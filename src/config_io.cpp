#include "citron/config_io.h"
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace citron::cfgio {
namespace {

std::string Sanitize(const std::string& name) {
    std::string out;
    for (char c : name) out += std::strchr("\\/:*?\"<>|", c) ? '_' : c;
    return out.empty() ? "config" : out;
}

// Settings that changed name or home since 1.x, so an old config keeps its numbers. A file written
// since never has the old names, so the old value can simply win.
void Migrate(const std::string& id, ModuleState& st, std::map<std::string, ModuleState>& mods) {
    auto move = [&](ModuleState& from, const char* a, ModuleState& to, const char* b) {
        if (auto f = from.sliders.find(a); f != from.sliders.end()) {
            to.sliders[b] = f->second;
            from.sliders.erase(f);
        }
    };
    if (id == "clicker") {
        ModuleState& right = mods["rightclicker"];
        move(st, "l_mincps", st, "cps_min");
        move(st, "l_maxcps", st, "cps_max");
        move(st, "l_jitter", st, "jitter");
        move(st, "r_mincps", right, "cps_min");
        move(st, "r_maxcps", right, "cps_max");
        move(st, "r_jitter", right, "jitter");
    }
    if (id == "aimassist") st.sliders.erase("vertical");
}

}  // namespace

std::string Dir() {
    static std::string cached;
    if (!cached.empty()) return cached;
    char buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH);
    const std::string dir = (n > 0 && n < MAX_PATH ? std::string(buf, n) : std::string(".")) + "\\CitronLite";
    CreateDirectoryA(dir.c_str(), nullptr);
    cached = dir + "\\";
    return cached;
}

std::wstring DirW() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    const std::wstring dir = (n > 0 && n < MAX_PATH ? std::wstring(buf, n) : std::wstring(L".")) + L"\\CitronLite";
    CreateDirectoryW(dir.c_str(), nullptr);
    cached = dir + L"\\";
    return cached;
}

std::string PathFor(const std::string& name) { return Dir() + Sanitize(name) + ".citron"; }

std::string StemOf(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    std::string n = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = n.find_last_of('.');
    return dot == std::string::npos ? n : n.substr(0, dot);
}

std::vector<std::string> List() {
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((Dir() + "*.citron").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(StemOf(fd.cFileName));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

std::string Serialize(const std::map<std::string, ModuleState>& mods, unsigned accent) {
    std::ostringstream out;
    char buf[160];
    std::snprintf(buf, sizeof buf, "citron-config 2\naccent %06X\n", accent & 0xFFFFFFu);
    out << buf;
    for (const auto& [id, st] : mods) {
        out << "[" << id << "]\non " << (st.on ? 1 : 0) << "\n";
        out << "key " << (st.key.empty() ? "none" : st.key) << "\n";
        for (const auto& [k, v] : st.sliders) {
            std::snprintf(buf, sizeof buf, "s %s %.6g\n", k.c_str(), v);
            out << buf;
        }
        for (const auto& [k, v] : st.checks) out << "c " << k << " " << (v ? 1 : 0) << "\n";
        for (const auto& [k, v] : st.choices) out << "h " << k << " " << v << "\n";
        for (const std::string& n : st.names)
            if (!n.empty() && n.find('\n') == std::string::npos) out << "n " << n << "\n";
    }
    return out.str();
}

bool Deserialize(const char* text, std::map<std::string, ModuleState>& mods, unsigned* accent) {
    if (!text) return false;
    std::istringstream lines(text);
    std::string line, id;
    ModuleState* st = nullptr;
    bool namesSeen = false;
    auto finish = [&] {
        if (st) Migrate(id, *st, mods);
    };
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '[') {
            const size_t end = line.find(']');
            if (end == std::string::npos) continue;
            finish();
            id = line.substr(1, end - 1);
            st = &mods[id];
            namesSeen = false;
            continue;
        }
        const size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const std::string type = line.substr(0, sp), rest = line.substr(sp + 1);
        if (type == "accent") {
            unsigned a = 0;
            if (accent && std::sscanf(rest.c_str(), "%x", &a) == 1) *accent = a & 0xFFFFFFu;
            continue;
        }
        if (!st) continue;
        if (type == "on") {
            st->on = std::atoi(rest.c_str()) != 0;
        } else if (type == "key") {
            st->key = rest == "none" || rest == "NONE" ? std::string() : rest;
        } else if (type == "n") {
            if (!namesSeen) st->names.clear();
            namesSeen = true;
            st->names.push_back(rest);
        } else {
            char k[96] = {};
            double v = 0;
            if (std::sscanf(rest.c_str(), "%95s %lf", k, &v) != 2) continue;
            if (type == "s") st->sliders[k] = (float)v;
            else if (type == "c") st->checks[k] = v != 0;
            else if (type == "h") st->choices[k] = (int)v;
        }
    }
    finish();
    return true;
}

void KeepKnown(std::map<std::string, ModuleState>& mods, const std::vector<ModuleDef>& defs) {
    std::map<std::string, ModuleState> out;
    for (const ModuleDef& d : defs) {
        ModuleState st = DefaultState(d);
        if (auto it = mods.find(d.key); it != mods.end()) {
            const ModuleState& from = it->second;
            st.on = d.hasToggle && from.on;
            st.key = from.key;
            for (const SliderDef& sd : d.sliders) {
                auto take = [&](const std::string& k) {
                    if (auto f = from.sliders.find(k); f != from.sliders.end() && std::isfinite(f->second))
                        st.sliders[k] = std::clamp(f->second, sd.min, sd.max);
                };
                if (!sd.range) {
                    take(sd.key);
                    continue;
                }
                const std::string lo = sd.key + "_min", hi = sd.key + "_max";
                take(lo);
                take(hi);
                if (st.sliders[lo] > st.sliders[hi]) std::swap(st.sliders[lo], st.sliders[hi]);
            }
            for (auto& [k, v] : st.checks)
                if (auto f = from.checks.find(k); f != from.checks.end()) v = f->second;
            for (const ChoiceDef& cd : d.choices)
                if (auto f = from.choices.find(cd.key);
                    f != from.choices.end() && f->second >= 0 && f->second < (int)cd.options.size())
                    st.choices[cd.key] = f->second;
            if (d.names) st.names = from.names;
        }
        out[d.key] = std::move(st);
    }
    mods = std::move(out);
}

bool Write(const std::string& path, const std::map<std::string, ModuleState>& mods, unsigned accent) {
    const std::string temp = path + ".tmp";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return false;
    const std::string text = Serialize(mods, accent);
    const bool complete = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool closed = std::fclose(f) == 0;
    if (!complete || !closed || !MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(temp.c_str());
        return false;
    }
    return true;
}

bool Read(const std::string& path, std::map<std::string, ModuleState>& mods, unsigned* accent) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string text;
    char chunk[4096];
    size_t r;
    while ((r = std::fread(chunk, 1, sizeof chunk, f)) > 0) text.append(chunk, r);
    std::fclose(f);
    return Deserialize(text.c_str(), mods, accent);
}

void Delete(const std::string& name) { DeleteFileA(PathFor(name).c_str()); }

Info Peek(const std::string& name) {
    Info info;
    const std::string path = PathFor(name);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fa)) {
        const unsigned long long t = (unsigned long long)fa.ftLastWriteTime.dwHighDateTime << 32 | fa.ftLastWriteTime.dwLowDateTime;
        info.savedAt = (long long)(t / 10000000ULL) - 11644473600LL;
    }
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char line[256];
        while (std::fgets(line, sizeof line, f)) info.enabled += std::strncmp(line, "on 1", 4) == 0;
        std::fclose(f);
    }
    return info;
}

std::string SaveDialog(const std::string& defaultName) {
    char file[MAX_PATH] = {};
    std::strncpy(file, defaultName.c_str(), MAX_PATH - 1);
    const std::string dir = Dir();
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = "Citron Lite config (*.citron)\0*.citron\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir.c_str();
    ofn.lpstrDefExt = "citron";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameA(&ofn) ? file : "";
}

std::string OpenDialog() {
    char file[MAX_PATH] = {};
    const std::string dir = Dir();
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = "Citron Lite config (*.citron)\0*.citron\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? file : "";
}

void OpenFolder() { ShellExecuteA(nullptr, "open", Dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

}  // namespace citron::cfgio
