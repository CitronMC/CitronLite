#include "citron/app.h"
#include "citron/config_io.h"
#include "citron/engine.h"
#include "citron/log.h"
#include "citron/platform.h"
#include "citron/telemetry.h"
#include "citron/updater.h"
#include "ui/draw.h"
#include "ui/rows.h"
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

bool CursorShowing() {
    CURSORINFO ci{};
    ci.cbSize = sizeof ci;
    return GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING);
}

// In gameplay: Bedrock in front with its cursor hidden, so a letter bind never fires while typing in chat.
bool InGame() { return platform::ForegroundIsBedrock(0) && !CursorShowing(); }

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
    if (gui != Gui::Closed) CloseGui(true);
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

void App::Tick() {
    ApplyFriendEdits();
    PollHotkeys();
    ClickGuiTick();
    NoteToggles();
    backend.Sync(s);
    Autosave();
}

void App::Frame(const FrameInput& in, FrameOutput& out) { DrawPanel(in, out); }

void* App::GameWindow() const {
    return platform::ForegroundIsBedrock(backend.GamePid()) ? GetForegroundWindow() : nullptr;
}

bool App::StreamHidden() const {
    const ModuleState* st = s.St("streamhide");
    return st && st->on;
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
    seenOn.clear();
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
uint32_t App::PressesOf(const std::string& id, int vk) {
    const uint32_t now = vk ? PhysKeyEdges(vk) : 0u;
    auto it = seenPresses.find(id);
    const uint32_t n = it != seenPresses.end() && it->second.first == vk ? now - it->second.second : 0u;
    seenPresses[id] = {vk, now};
    return n;
}

void App::PollHotkeys() {
    const bool liteFront = hwnd && GetForegroundWindow() == (HWND)hwnd;
    const bool blocked = !s.listening.empty() || ui::Editing();
    const bool live = !justBound && !blocked && (liteFront || InGame());

    // These two answer whatever has focus: one brings a hidden window back, the other is a panic key.
    if (ModuleState* t = s.St("taskbarhide")) {
        const uint32_t n = PressesOf("taskbarhide", LabelToVK(t->key));
        if (!blocked && (n & 1u)) t->on = !t->on;
    }
    if (ModuleState* sd = s.St("selfdestruct")) {
        const uint32_t n = PressesOf("selfdestruct", LabelToVK(sd->key));
        if (!blocked && n) {
            SelfDestruct();
            return;
        }
    }
    for (const ModuleDef& d : s.defs) {
        if (d.key == "taskbarhide" || d.key == "selfdestruct" || d.key == "clickgui") continue;
        ModuleState* st = s.St(d.key);
        if (!st) continue;
        const uint32_t n = PressesOf(d.key, LabelToVK(st->key));
        if (live && d.hasToggle && d.bind == BindKind::Toggle && (n & 1u)) st->on = !st->on;
    }
}

// The Click GUI. An outside program cannot stop the mouse reaching the game: the game reads it raw,
// past anything a hook can hold back. So it asks the game itself: the chat screen opens behind the GUI,
// which frees the pointer and stills the camera, and everything on it is out of the way of the clicks,
// which land on the overlay. Should chat not come, the pause screen does the same.
void App::ClickGuiTick() {
    const ModuleState* key = s.St("clickgui");
    const uint32_t pressed = PressesOf("clickgui", key ? LabelToVK(key->key) : 0);
    const uint32_t escape = PressesOf("gui:esc", VK_ESCAPE), enter = PressesOf("gui:enter", VK_RETURN);
    const bool bound = justBound;
    justBound = false;
    const unsigned long long now = GetTickCount64();
    if (gui == Gui::Closed) {
        if (pressed && !bound && s.listening.empty() && !ui::Editing() && InGame() && backend.PressChat()) {
            gui = Gui::Opening;
            guiAt = now;
            guiTriedPause = false;
            SetKeysTaken(true);
            backend.SetGuiOpen(true);
        }
        return;
    }
    if (!GameWindow()) {
        CloseGui(false);
        return;
    }
    if (gui == Gui::Opening) {
        if (CursorShowing()) {
            gui = Gui::Open;
        } else if (now - guiAt > 700) {
            if (guiTriedPause) {
                CloseGui(false);
            } else {
                guiTriedPause = true;
                guiAt = now;
                backend.PressKey(VK_ESCAPE);
            }
        }
        return;
    }
    if (s.listening.empty()) {
        guiPressSeen = KeyPressCount();
        if (pressed || escape || enter) CloseGui(true);
        return;
    }
    // a key for a module: the next press, a left click cancels and Escape clears
    if (const uint32_t presses = KeyPressCount(); presses != guiPressSeen) {
        guiPressSeen = presses;
        const int vk = LastKeyPress();
        ModuleState* st = s.St(s.listening);
        if (vk == VK_ESCAPE) {
            if (st) st->key.clear();
        } else if (vk != VK_LBUTTON) {
            const std::string label = LabelForVK(vk);
            if (label.empty()) return;
            if (st) st->key = label;
        }
        s.listening.clear();
        justBound = true;
    }
}

void App::CloseGui(bool backToGame) {
    SetKeysTaken(false);
    backend.SetGuiOpen(false);
    gui = Gui::Closed;
    s.listening.clear();
    if (backToGame) backend.PressKey(VK_ESCAPE);
}

// Notifications.
void App::PushToast(const std::string& title, const std::string& state, bool lit) {
    const auto same = std::find_if(toasts.begin(), toasts.end(), [&](const Toast& t) { return t.title == title; });
    if (same != toasts.end()) toasts.erase(same);
    toasts.push_back({title, state, lit, GetTickCount64()});
    if (toasts.size() > 5) toasts.erase(toasts.begin());
}

void App::NoteToggles() {
    const unsigned long long now = GetTickCount64();
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(),
                                [&](const Toast& t) { return (float)(now - t.atMs) / 1000.f >= kToastSeconds; }),
                 toasts.end());
    const bool show = ui::StateOn(s, "notifications") && gui == Gui::Closed && GameWindow();
    for (const ModuleDef& d : s.defs) {
        const ModuleState* st = d.cat == Cat::Settings ? nullptr : s.St(d.key);
        if (!st) continue;
        const auto seen = seenOn.find(d.key);
        if (seen == seenOn.end()) {
            seenOn[d.key] = st->on;
        } else if (seen->second != st->on) {
            seen->second = st->on;
            if (show) PushToast(d.name, st->on ? "On" : "Off", st->on);
        }
    }
}

void App::ApplyFriendEdits() {
    ModuleState* f = s.St("friends");
    for (const std::string& name : backend.TakeFriendEdits()) {
        if (!f) continue;
        auto it = std::find_if(f->names.begin(), f->names.end(), [&](const std::string& n) { return Lower(n) == Lower(name); });
        const bool removed = it != f->names.end();
        if (removed) f->names.erase(it);
        else f->names.push_back(name);
        if (ui::StateOn(s, "notifications")) PushToast(name, removed ? "Removed" : "Added", !removed);
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
