#include "citron/app.h"
#include "citron/config_io.h"
#include "citron/engine.h"
#include "citron/log.h"
#include "citron/platform.h"
#include "citron/telemetry.h"
#include "citron/updater.h"
#include "ui/draw.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace citron {
namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string LastConfigPath() { return cfgio::Dir() + "last-config.txt"; }

// In gameplay: Bedrock in front with its cursor hidden, so a letter bind never fires while typing in chat.
bool InGame() {
    if (!platform::ForegroundIsBedrock(0)) return false;
    CURSORINFO ci{};
    ci.cbSize = sizeof ci;
    return GetCursorInfo(&ci) && !(ci.flags & CURSOR_SHOWING);
}

}  // namespace

void App::Init(bool isHeadless) {
    headless = isHeadless;
    BuildModel(s);
    if (headless) {
        s.configs = {"Default"};
        s.activeConfig = "Default";
        return;
    }
    LoadConfigs();
    updater::Start();
    telemetry::Start();
    backend.Init();
}

void App::Shutdown() {
    if (!headless) SaveActive();
    backend.Shutdown();
    if (!headless) {
        telemetry::Stop();
        updater::Stop();
    }
}

void App::SetHwnd(void* h) {
    hwnd = h;
    backend.SetHwnd(h);
}

void App::Preview(const std::string& key, const std::string& bind) {
    if (ModuleState* st = s.St(key)) {
        st->on = true;
        if (!bind.empty()) st->key = bind;
    }
}

void App::SaveTo(const std::string& path) const { cfgio::Write(path, s.mods, s.accent); }

void App::Frame(const FrameInput& in, FrameOutput& out) {
    ApplyFriendEdits();
    PollHotkeys();
    DrawPanel(in, out);
    backend.Sync(s);
    Autosave();
}

// Configs.
void App::LoadConfigs() {
    s.configs = cfgio::List();
    if (s.configs.empty()) {
        s.configs = {"Default"};
        cfgio::Write(cfgio::PathFor("Default"), s.mods, s.accent);
    }
    std::string last;
    if (FILE* f = std::fopen(LastConfigPath().c_str(), "r")) {
        char buf[128] = {};
        if (std::fgets(buf, sizeof buf, f)) last = Trim(std::string(buf).substr(0, std::string(buf).find_first_of("\r\n")));
        std::fclose(f);
    }
    const bool known = std::find(s.configs.begin(), s.configs.end(), last) != s.configs.end();
    LoadConfig(known ? last : s.configs.front());
}

bool App::LoadConfig(const std::string& name) {
    AppState fresh;
    BuildModel(fresh);
    unsigned accent = 0;
    if (!cfgio::Read(cfgio::PathFor(name), fresh.mods, &accent) && name != "Default") return false;
    cfgio::KeepKnown(fresh.mods, fresh.defs);
    s.mods = fresh.mods;
    s.accent = accent;
    s.activeConfig = name;
    autosaveBaseline = autosaveSeen = cfgio::Serialize(s.mods, s.accent);
    autosaveDirtySince = 0;
    RememberActive();
    return true;
}

void App::RememberActive() {
    if (headless) return;
    if (FILE* f = std::fopen(LastConfigPath().c_str(), "w")) {
        std::fprintf(f, "%s\n", s.activeConfig.c_str());
        std::fclose(f);
    }
}

void App::SaveActive() {
    if (headless || s.activeConfig.empty()) return;
    cfgio::Write(cfgio::PathFor(s.activeConfig), s.mods, s.accent);
    autosaveBaseline = autosaveSeen = cfgio::Serialize(s.mods, s.accent);
    autosaveDirtySince = 0;
}

// Edits are written once they have settled for a moment, so a crash loses at most the last one.
void App::Autosave() {
    if (headless || s.activeConfig.empty()) return;
    const std::string now = cfgio::Serialize(s.mods, s.accent);
    if (now == autosaveBaseline) {
        autosaveSeen = now;
        autosaveDirtySince = 0;
        return;
    }
    const DWORD tick = GetTickCount();
    if (now != autosaveSeen) {
        autosaveSeen = now;
        autosaveDirtySince = tick;
        return;
    }
    if (tick - autosaveDirtySince >= 750) SaveActive();
}

void App::NewConfig() {
    SaveActive();
    std::string name;
    for (int n = 1;; ++n) {
        name = "Config " + std::to_string(n);
        bool taken = false;
        for (const auto& c : s.configs) taken |= Lower(c) == Lower(name);
        if (!taken) break;
    }
    s.configs.push_back(name);
    s.activeConfig = name;
    SaveActive();
    RememberActive();
}

bool App::RenameConfig(const std::string& from, const std::string& to) {
    const std::string name = Trim(to);
    if (name.empty() || name == from) return name == from;
    for (const auto& c : s.configs)
        if (c != from && Lower(c) == Lower(name)) return false;
    std::map<std::string, ModuleState> mods = s.mods;
    unsigned accent = s.accent;
    if (from != s.activeConfig) {
        AppState fresh;
        BuildModel(fresh);
        mods = fresh.mods;
        accent = 0;
        cfgio::Read(cfgio::PathFor(from), mods, &accent);
        cfgio::KeepKnown(mods, fresh.defs);
    }
    if (!headless) {
        cfgio::Write(cfgio::PathFor(name), mods, accent);
        if (Lower(cfgio::PathFor(from)) != Lower(cfgio::PathFor(name))) cfgio::Delete(from);
    }
    for (auto& c : s.configs)
        if (c == from) c = name;
    if (s.activeConfig == from) {
        s.activeConfig = name;
        RememberActive();
    }
    return true;
}

void App::DeleteConfig(const std::string& name) {
    if (!headless) cfgio::Delete(name);
    s.configs.erase(std::remove(s.configs.begin(), s.configs.end(), name), s.configs.end());
    if (s.configs.empty()) {
        AppState fresh;
        BuildModel(fresh);
        s.mods = fresh.mods;
        s.accent = 0;
        s.configs = {"Default"};
        s.activeConfig = "Default";
        SaveActive();
        RememberActive();
        return;
    }
    if (s.activeConfig == name) LoadConfig(s.configs.front());
}

void App::ImportConfig() {
    const std::string path = cfgio::OpenDialog();
    if (path.empty()) return;
    AppState fresh;
    BuildModel(fresh);
    unsigned accent = 0;
    if (!cfgio::Read(path, fresh.mods, &accent)) return;
    cfgio::KeepKnown(fresh.mods, fresh.defs);
    std::string base = Trim(cfgio::StemOf(path));
    if (base.empty()) base = "Imported";
    std::string name = base;
    for (int n = 2;; ++n) {
        bool taken = false;
        for (const auto& c : s.configs) taken |= Lower(c) == Lower(name);
        if (!taken) break;
        name = base + " " + std::to_string(n);
    }
    SaveActive();
    cfgio::Write(cfgio::PathFor(name), fresh.mods, accent);
    s.configs.push_back(name);
    LoadConfig(name);
}

void App::ExportConfig() {
    SaveActive();
    const std::string path = cfgio::SaveDialog(s.activeConfig + ".citron");
    if (!path.empty()) cfgio::Write(path, s.mods, s.accent);
}

// Hotkeys.
// Presses are counted by the low-level hooks as they happen, so a slow frame never misses a tap; a
// toggle flips once per press since the last look.
void App::PollHotkeys() {
    const bool liteFront = hwnd && GetForegroundWindow() == (HWND)hwnd;
    const bool blocked = !s.listening.empty() || ui::Editing();
    const bool live = !justBound && !blocked && (liteFront || InGame());
    justBound = false;

    auto pending = [&](const std::string& id, int vk) -> uint32_t {
        const uint32_t now = vk ? PhysKeyEdges(vk) : 0u;
        auto it = seenPresses.find(id);
        const uint32_t n = it != seenPresses.end() && it->second.first == vk ? now - it->second.second : 0u;
        seenPresses[id] = {vk, now};
        return n;
    };

    // These two answer whatever has focus: one brings a hidden window back, the other is a panic key.
    if (ModuleState* t = s.St("taskbarhide")) {
        const uint32_t n = pending("taskbarhide", LabelToVK(t->key));
        if (!blocked && (n & 1u)) t->on = !t->on;
    }
    if (ModuleState* sd = s.St("selfdestruct")) {
        const uint32_t n = pending("selfdestruct", LabelToVK(sd->key));
        if (!blocked && n) {
            SelfDestruct();
            return;
        }
    }
    for (const ModuleDef& d : s.defs) {
        if (d.key == "taskbarhide" || d.key == "selfdestruct") continue;
        ModuleState* st = s.St(d.key);
        if (!st) continue;
        const uint32_t n = pending(d.key, LabelToVK(st->key));
        if (live && d.hasToggle && d.bind == BindKind::Toggle && (n & 1u)) st->on = !st->on;
    }
}

void App::ApplyFriendEdits() {
    ModuleState* f = s.St("friends");
    for (const std::string& name : backend.TakeFriendEdits()) {
        if (!f) continue;
        auto it = std::find_if(f->names.begin(), f->names.end(), [&](const std::string& n) { return Lower(n) == Lower(name); });
        if (it != f->names.end()) f->names.erase(it);
        else f->names.push_back(name);
    }
}

// Puts everything back in the game, then closes; optionally takes Minecraft, the exe and Lite's
// data with it. The deletes run from a small script once this process has let go of the files.
void App::SelfDestruct() {
    const ModuleState* sd = s.St("selfdestruct");
    auto opt = [&](const char* k) {
        if (!sd) return false;
        const auto it = sd->checks.find(k);
        return it != sd->checks.end() && it->second;
    };
    const bool closeGame = opt("closeGame"), deleteExe = opt("deleteExe"), wipeData = opt("wipeData");
    if (closeGame)
        if (const DWORD pid = engine::FindPid("Minecraft.Windows.exe"))
            if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid)) {
                TerminateProcess(h, 0);
                CloseHandle(h);
            }
    if (deleteExe || wipeData) {
        wchar_t exe[MAX_PATH] = {}, temp[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exe, MAX_PATH) && GetTempPathW(MAX_PATH, temp)) {
            const DWORD pid = GetCurrentProcessId();
            const std::wstring bat = std::wstring(temp) + L"citron_cleanup_" + std::to_wstring(pid) + L".bat";
            std::wstring script = L"@echo off\r\nchcp 65001 >nul\r\n:wait\r\n";
            script += L"tasklist /FI \"PID eq " + std::to_wstring(pid) + L"\" 2>nul | find \"" + std::to_wstring(pid) + L"\" >nul\r\n";
            script += L"if not errorlevel 1 (\r\n  ping 127.0.0.1 -n 2 >nul\r\n  goto wait\r\n)\r\n";
            if (wipeData) script += L"rmdir /S /Q \"" + cfgio::DirW() + L"\" >nul 2>&1\r\n";
            if (deleteExe) script += L"del /F /Q \"" + std::wstring(exe) + L"\" >nul 2>&1\r\n";
            script += L"del /F /Q \"%~f0\" >nul 2>&1\r\n";
            HANDLE f = CreateFileW(bat.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                const int need = WideCharToMultiByte(CP_UTF8, 0, script.c_str(), (int)script.size(), nullptr, 0, nullptr, nullptr);
                std::string utf8(need > 0 ? need : 0, '\0');
                if (need > 0) WideCharToMultiByte(CP_UTF8, 0, script.c_str(), (int)script.size(), utf8.data(), need, nullptr, nullptr);
                DWORD wrote = 0;
                WriteFile(f, utf8.data(), (DWORD)utf8.size(), &wrote, nullptr);
                CloseHandle(f);
                std::wstring cmd = L"cmd.exe /c \"" + bat + L"\"";
                STARTUPINFOW si{};
                si.cb = sizeof si;
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi{};
                if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr,
                                   nullptr, &si, &pi)) {
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                }
            }
        }
    }
    PostQuitMessage(0);
}

}  // namespace citron
