#include "citron/updater.h"
#include "citron/config_io.h"
#include "citron/lite_manifest.h"
#include "citron/log.h"
#include "citron/version.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace citron::updater {
namespace {

// The manifest names the current build and its hash; the build sits beside it on the same host,
// and nothing in the manifest can send the download anywhere else.
constexpr wchar_t kHost[]         = L"citron.lol";
constexpr wchar_t kManifestPath[] = L"/citron_lite_update.json";

// Anything smaller is an error page or a cut off transfer, never a build.
constexpr DWORD kMinPlausibleBytes = 1u << 20;   // 1 MiB
constexpr DWORD kMaxManifestBytes  = 4096;

enum class Job { None, Check, Install };

std::atomic<bool> g_enabled{true};
std::atomic<bool> g_running{false};
std::atomic<Job>  g_job{Job::None};
std::atomic<HINTERNET> g_activeRequest{nullptr};

std::thread g_worker;
std::mutex g_waitMutex;
std::condition_variable g_wait;

std::mutex g_statusMutex;
Status g_status;
Release g_release;

void SetStatus(Stage stage, const std::string& detail, const std::string& latest, int percent) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    g_status.stage = stage;
    g_status.detail = detail;
    if (!latest.empty()) g_status.latest = latest;
    g_status.percent = percent;
}

void SetPercent(int percent) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    g_status.percent = percent;
}

// Setting.

std::string SettingPath() { return cfgio::Dir() + "auto-update.txt"; }

bool LoadEnabled() {
    FILE* f = std::fopen(SettingPath().c_str(), "r");
    if (!f) return true;                       // on unless the user turned it off
    int value = 1;
    const bool ok = std::fscanf(f, "%d", &value) == 1;
    std::fclose(f);
    return !ok || value != 0;
}

void SaveEnabled(bool enabled) {
    if (FILE* f = std::fopen(SettingPath().c_str(), "w")) {
        std::fprintf(f, "%d\n", enabled ? 1 : 0);
        std::fclose(f);
    }
}

// Paths.

std::wstring ExePath() {
    wchar_t buf[MAX_PATH * 2] = {0};
    const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    if (!n || n >= sizeof buf / sizeof buf[0]) return {};
    return buf;
}

// HTTP.

// One place that owns the three handles, so no path out of a request can leak them.
struct Call {
    HINTERNET session = nullptr, connect = nullptr, request = nullptr;
    ~Call() {
        if (request) {
            HINTERNET expected = request;
            if (g_activeRequest.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel))
                WinHttpCloseHandle(request);   // Stop() may have closed it first
        }
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
    }
};

bool Send(Call& call, const wchar_t* path, DWORD receiveTimeoutMs) {
    call.session = WinHttpOpen(L"CitronLite-Updater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!call.session) return false;
    WinHttpSetTimeouts(call.session, 5000, 5000, 5000, (int)receiveTimeoutMs);
    call.connect = WinHttpConnect(call.session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!call.connect) return false;
    call.request = WinHttpOpenRequest(call.connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!call.request) return false;
    g_activeRequest.store(call.request, std::memory_order_release);
    return WinHttpSendRequest(call.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
           WinHttpReceiveResponse(call.request, nullptr);
}

DWORD StatusCode(HINTERNET request) {
    DWORD code = 0, size = sizeof code;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
    return code;
}

DWORD ContentLength(HINTERNET request) {
    DWORD length = 0, size = sizeof length;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX);
    return length;
}

bool FetchManifest(Release& out, std::string& error) {
    Call call;
    if (!Send(call, kManifestPath, 8000)) { error = "Could not reach the update server"; return false; }
    const DWORD code = StatusCode(call.request);
    if (code != 200) {
        error = code == 404 ? "No update has been published yet" : "The update server said no";
        Log("[updater] manifest returned %lu", (unsigned long)code);
        return false;
    }
    std::string body;
    char buffer[1024];
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(call.request, buffer, sizeof buffer, &read) || !read) break;
        body.append(buffer, read);
        if (body.size() > kMaxManifestBytes) { error = "The update file is malformed"; return false; }
    }
    return ParseManifest(body, out, error);
}

// Download.

// Hashes as it writes, so the file is never read back off disk to be checked.
struct Sha256 {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;

    Sha256() {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return;
        ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    }
    ~Sha256() {
        if (hash) BCryptDestroyHash(hash);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
    void Feed(const void* data, DWORD bytes) {
        if (ok) ok = BCryptHashData(hash, (PUCHAR)data, bytes, 0) >= 0;
    }
    std::string Hex() {
        unsigned char digest[32]{};
        if (!ok || BCryptFinishHash(hash, digest, sizeof digest, 0) < 0) return {};
        static constexpr char lut[] = "0123456789abcdef";
        std::string out(64, '0');
        for (size_t i = 0; i < sizeof digest; ++i) {
            out[i * 2] = lut[digest[i] >> 4];
            out[i * 2 + 1] = lut[digest[i] & 15];
        }
        return out;
    }
};

bool Download(const Release& release, const std::wstring& dest, std::string& error) {
    Call call;
    const std::wstring path(release.path.begin(), release.path.end());
    if (!Send(call, path.c_str(), 30000)) { error = "Download did not start"; return false; }
    if (const DWORD code = StatusCode(call.request); code != 200) {
        error = "The download is missing from the server";
        Log("[updater] asset returned %lu", (unsigned long)code);
        return false;
    }
    const DWORD expected = ContentLength(call.request);

    HANDLE file = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = "Cannot write next to the client"; return false; }

    Sha256 digest;
    std::vector<char> buffer(64 * 1024);
    DWORD total = 0;
    bool ok = true;
    while (ok) {
        if (!g_running.load(std::memory_order_acquire)) { error = "Cancelled"; ok = false; break; }
        DWORD read = 0;
        if (!WinHttpReadData(call.request, buffer.data(), (DWORD)buffer.size(), &read)) {
            error = "Download was interrupted"; ok = false; break;
        }
        if (!read) break;                        // clean end of body
        DWORD written = 0;
        if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read) {
            error = "Ran out of disk space"; ok = false; break;
        }
        digest.Feed(buffer.data(), read);
        total += read;
        if (expected) SetPercent((int)((total * 100ull) / expected));
    }
    CloseHandle(file);

    // The hash is the check; the sizes only give a cut off transfer a clearer message.
    if (ok && expected && total != expected) {
        error = "Download ended early";
        Log("[updater] got %lu of %lu bytes", (unsigned long)total, (unsigned long)expected);
        ok = false;
    }
    if (ok && total < kMinPlausibleBytes) { error = "Download ended early"; ok = false; }
    if (ok && digest.Hex() != release.sha256) {
        error = "The download did not match its checksum";
        Log("[updater] sha256 mismatch");
        ok = false;
    }

    if (!ok) DeleteFileW(dest.c_str());
    return ok;
}

// Install.

// A running exe cannot be deleted but can be renamed, and keeps running under the new name.
bool Swap(const std::wstring& exe, const std::wstring& staged, std::string& error) {
    const std::wstring previous = exe + L".old";
    DeleteFileW(previous.c_str());
    if (!MoveFileExW(exe.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        error = "Cannot replace the client here. Move it out of a protected folder";
        Log("[updater] rename away failed, err=%lu", (unsigned long)GetLastError());
        return false;
    }
    if (!MoveFileExW(staged.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(previous.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);   // put it back
        error = "Cannot replace the client here. Move it out of a protected folder";
        Log("[updater] rename in failed, err=%lu", (unsigned long)GetLastError());
        return false;
    }
    return true;
}

void DoInstall(const Release& release) {
    const std::wstring exe = ExePath();
    if (exe.empty()) { SetStatus(Stage::Failed, "Cannot find the client on disk", release.name, 0); return; }
    const std::wstring staged = exe + L".new";

    SetStatus(Stage::Downloading, "", release.name, 0);
    std::string error;
    if (!Download(release, staged, error)) {
        if (!g_running.load(std::memory_order_acquire)) return;   // shutting down, say nothing
        SetStatus(Stage::Failed, error, release.name, 0);
        return;
    }
    if (!Swap(exe, staged, error)) {
        DeleteFileW(staged.c_str());
        SetStatus(Stage::Failed, error, release.name, 0);
        return;
    }
    Log("[updater] installed %s", release.name.c_str());
    SetStatus(Stage::Ready, "", release.name, 100);
}

void DoCheck(bool installIfNewer) {
    SetStatus(Stage::Checking, "", "", 0);
    Release release;
    std::string error;
    if (!FetchManifest(release, error)) {
        if (!g_running.load(std::memory_order_acquire)) return;
        SetStatus(Stage::Failed, error, "", 0);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        g_release = release;
    }
    Log("[updater] published %d (%s), running %d (%s)", release.version, release.name.c_str(),
        kVersion, kVersionName);
    if (release.version <= kVersion) { SetStatus(Stage::UpToDate, "", release.name, 0); return; }
    if (installIfNewer) DoInstall(release);
    else SetStatus(Stage::Available, "", release.name, 0);
}

void Worker() {
    std::unique_lock<std::mutex> lock(g_waitMutex);
    while (g_running.load(std::memory_order_acquire)) {
        const Job job = g_job.exchange(Job::None, std::memory_order_acq_rel);
        if (job == Job::None) {
            g_wait.wait(lock, [] {
                return !g_running.load(std::memory_order_acquire) ||
                       g_job.load(std::memory_order_acquire) != Job::None;
            });
            continue;
        }
        lock.unlock();
        if (job == Job::Check) {
            DoCheck(Enabled());
        } else if (job == Job::Install) {
            Release pending;
            {
                std::lock_guard<std::mutex> guard(g_statusMutex);
                pending = g_release;
            }
            if (pending.version) DoInstall(pending);
        }
        lock.lock();
    }
}

void Post(Job job) {
    if (!g_running.load(std::memory_order_acquire)) return;
    g_job.store(job, std::memory_order_release);
    g_wait.notify_all();
}

} // namespace

void Start() {
    if (g_running.exchange(true, std::memory_order_acq_rel)) return;
    g_enabled.store(LoadEnabled(), std::memory_order_relaxed);

    // What the last update left: the old build, deletable now, and a download whose install failed.
    if (const std::wstring exe = ExePath(); !exe.empty()) {
        DeleteFileW((exe + L".old").c_str());
        DeleteFileW((exe + L".new").c_str());
    }

    g_worker = std::thread(Worker);
    if (g_enabled.load(std::memory_order_relaxed)) Post(Job::Check);
}

void Stop() {
    if (!g_running.exchange(false, std::memory_order_acq_rel)) return;
    // A download in flight is cancelled, not waited for.
    if (HINTERNET request = g_activeRequest.exchange(nullptr, std::memory_order_acq_rel))
        WinHttpCloseHandle(request);
    g_wait.notify_all();
    if (g_worker.joinable()) g_worker.join();
}

bool Enabled() { return g_enabled.load(std::memory_order_relaxed); }

void SetEnabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_relaxed);
    SaveEnabled(enabled);
    if (enabled && Get().stage == Stage::Idle) Post(Job::Check);
}

Status Get() {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    return g_status;
}

void CheckNow() { Post(Job::Check); }

void InstallNow() {
    if (Get().stage == Stage::Available) Post(Job::Install);
}

bool RestartToApply() {
    if (Get().stage != Stage::Ready) return false;
    const std::wstring exe = ExePath();
    if (exe.empty()) return false;
    std::wstring dir = exe;
    if (const size_t slash = dir.find_last_of(L"\\/"); slash != std::wstring::npos) dir.erase(slash);

    // Only one instance may run, so the new one waits for this one to end.
    wchar_t args[64];
    std::swprintf(args, sizeof args / sizeof args[0], L"--await-pid=%lu",
                  (unsigned long)GetCurrentProcessId());
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", exe.c_str(), args, dir.c_str(),
                                           SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}

} // namespace citron::updater
