#include "citron/telemetry.h"
#include "citron/config_io.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

namespace citron::telemetry {
namespace {

std::atomic<bool> g_enabled{true};
std::atomic<bool> g_running{false};
std::atomic<unsigned long> g_revision{0};
std::thread g_worker;
std::mutex g_waitMutex;
std::condition_variable g_wait;
std::atomic<HINTERNET> g_activeRequest{nullptr};

std::string SettingPath() {
    return cfgio::Dir() + "anonymous-presence.txt";
}

bool LoadEnabled() {
    FILE* f = std::fopen(SettingPath().c_str(), "r");
    if (!f) return true;
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

std::string NewRunId() {
    unsigned char bytes[16]{};
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return {};
    static constexpr char hex[] = "0123456789abcdef";
    std::string out(32, '0');
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        out[i * 2] = hex[bytes[i] >> 4];
        out[i * 2 + 1] = hex[bytes[i] & 15];
    }
    SecureZeroMemory(bytes, sizeof(bytes));
    return out;
}

void SendPresence(const std::string& runId) {
    if (runId.empty() || !g_running.load(std::memory_order_acquire)) return;
    HINTERNET session = WinHttpOpen(L"Citron Lite Presence/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return;
    if (!g_running.load(std::memory_order_acquire)) {
        WinHttpCloseHandle(session);
        return;
    }
    WinHttpSetTimeouts(session, 1500, 1500, 1500, 1500);

    HINTERNET connect = WinHttpConnect(session, L"api.citron.lol", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return;
    }
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", L"/api/v1/lite/presence",
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (request) {
        g_activeRequest.store(request, std::memory_order_release);
        auto closeRequest = [&] {
            HINTERNET expected = request;
            if (g_activeRequest.compare_exchange_strong(expected, nullptr,
                                                        std::memory_order_acq_rel))
                WinHttpCloseHandle(request);
        };
        if (g_running.load(std::memory_order_acquire)) {
            const std::string body = "{\"runId\":\"" + runId + "\"}";
            static constexpr wchar_t headers[] = L"Content-Type: application/json\r\n";
            if (WinHttpSendRequest(request, headers, (DWORD)-1L,
                                   (void*)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0))
                WinHttpReceiveResponse(request, nullptr);
        }
        closeRequest();
    }
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

void Worker() {
    const std::string runId = NewRunId();
    std::unique_lock<std::mutex> lock(g_waitMutex);
    while (g_running.load(std::memory_order_acquire)) {
        const bool enabled = g_enabled.load(std::memory_order_relaxed);
        const unsigned long revision = g_revision.load(std::memory_order_relaxed);
        lock.unlock();
        if (enabled) SendPresence(runId);
        lock.lock();
        g_wait.wait_for(lock, enabled ? std::chrono::seconds(45) : std::chrono::seconds(2), [revision] {
            return !g_running.load(std::memory_order_acquire) ||
                   g_revision.load(std::memory_order_relaxed) != revision;
        });
    }
}

} // namespace

void Start() {
    if (g_running.exchange(true, std::memory_order_acq_rel)) return;
    g_enabled.store(LoadEnabled(), std::memory_order_relaxed);
    g_worker = std::thread(Worker);
}

void Stop() {
    if (!g_running.exchange(false, std::memory_order_acq_rel)) return;
    if (HINTERNET request = g_activeRequest.exchange(nullptr, std::memory_order_acq_rel))
        WinHttpCloseHandle(request);
    g_wait.notify_all();
    if (g_worker.joinable()) g_worker.join();
}

bool Enabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

void SetEnabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_relaxed);
    SaveEnabled(enabled);
    g_revision.fetch_add(1, std::memory_order_relaxed);
    g_wait.notify_all();
}

} // namespace citron::telemetry
