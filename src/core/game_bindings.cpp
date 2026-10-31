#include "citron/game_bindings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>

namespace citron::gamebind {
namespace {

struct Cache {
    DWORD pid = 0;
    ULONGLONG checkedAt = 0;
    std::wstring path;
    FILETIME writeTime{};
    Bindings bindings{};
};

Cache g_cache;
std::mutex g_cacheMutex;

std::wstring Directory(std::wstring path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}

bool FileInfo(const std::wstring& path, WIN32_FILE_ATTRIBUTE_DATA& info) {
    return !path.empty() && GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
           !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

void Consider(const std::wstring& path, std::wstring& best, FILETIME& bestTime) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!FileInfo(path, info)) return;
    if (best.empty() || CompareFileTime(&info.ftLastWriteTime, &bestTime) > 0) {
        best = path;
        bestTime = info.ftLastWriteTime;
    }
}

void SearchUsers(const std::wstring& usersRoot, std::wstring& best, FILETIME& bestTime) {
    WIN32_FIND_DATAW entry{};
    HANDLE find = FindFirstFileW((usersRoot + L"\\*").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            std::wcscmp(entry.cFileName, L".") == 0 || std::wcscmp(entry.cFileName, L"..") == 0 ||
            std::wcscmp(entry.cFileName, L"Shared") == 0)
            continue;
        Consider(usersRoot + L"\\" + entry.cFileName +
                 L"\\games\\com.mojang\\minecraftpe\\options.txt", best, bestTime);
    } while (FindNextFileW(find, &entry));
    FindClose(find);
}

std::wstring Environment(const wchar_t* name) {
    wchar_t value[32768]{};
    DWORD n = GetEnvironmentVariableW(name, value, (DWORD)(sizeof(value) / sizeof(value[0])));
    return n && n < sizeof(value) / sizeof(value[0]) ? std::wstring(value, n) : std::wstring{};
}

std::wstring FindOptions(HANDLE process, FILETIME& writeTime) {
    std::wstring best;
    wchar_t image[32768]{};
    DWORD imageLen = (DWORD)(sizeof(image) / sizeof(image[0]));
    if (process && QueryFullProcessImageNameW(process, 0, image, &imageLen)) {
        const std::wstring exeDir = Directory(std::wstring(image, imageLen));
        SearchUsers(exeDir + L"\\Minecraft Bedrock\\Users", best, writeTime);
        SearchUsers(exeDir + L"\\Users", best, writeTime);
    }

    const std::wstring roaming = Environment(L"APPDATA");
    if (!roaming.empty())
        SearchUsers(roaming + L"\\Minecraft Bedrock\\Users", best, writeTime);

    const std::wstring local = Environment(L"LOCALAPPDATA");
    if (!local.empty()) {
        const std::wstring state = local +
            L"\\Packages\\Microsoft.MinecraftUWP_8wekyb3d8bbwe\\LocalState";
        Consider(state + L"\\games\\com.mojang\\minecraftpe\\options.txt", best, writeTime);
        SearchUsers(state + L"\\Users", best, writeTime);
    }
    return best;
}

void Assign(Bindings& out, const char* action, int value) {
    if (std::strcmp(action, "attack") == 0) out.attack = value;
    else if (std::strcmp(action, "use") == 0) out.use = value;
    else if (std::strcmp(action, "forward") == 0) out.forward = value;
    else if (std::strcmp(action, "back") == 0) out.back = value;
    else if (std::strcmp(action, "left") == 0) out.left = value;
    else if (std::strcmp(action, "right") == 0) out.right = value;
    else if (std::strcmp(action, "jump") == 0) out.jump = value;
    else if (std::strcmp(action, "sneak") == 0) out.sneak = value;
    else if (std::strcmp(action, "sprint") == 0) out.sprint = value;
    else if (std::strcmp(action, "inventory") == 0) out.inventory = value;
    else if (std::strcmp(action, "chat") == 0) out.chat = value;
    else if (std::strcmp(action, "drop") == 0) out.drop = value;
    else if (std::strncmp(action, "hotbar.", 7) == 0) {
        const int slot = std::atoi(action + 7) - 1;
        if (slot >= 0 && slot < 9) out.hotbar[slot] = value;
    }
}

Bindings ReadBindings(const std::wstring& path) {
    Bindings out;
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) return out;
    constexpr const char* prefix = "keyboard_type_0_key.";
    char line[512]{};
    while (std::fgets(line, (int)sizeof(line), file)) {
        if (std::strncmp(line, prefix, std::strlen(prefix)) != 0) continue;
        char* colon = std::strchr(line, ':');
        if (!colon) continue;
        *colon = 0;
        char* end = nullptr;
        const long value = std::strtol(colon + 1, &end, 10); // first binding when a comma-list exists
        if (end == colon + 1) continue;
        Assign(out, line + std::strlen(prefix), (int)value);
    }
    std::fclose(file);
    out.loaded = true;
    return out;
}

// The game's mouse codes: -99 left, -98 right, -97 middle, nothing at -96, -95 and -94 the side buttons.
DWORD MouseFlag(int code, bool down) {
    if (code == -99) return down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    if (code == -98) return down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    if (code == -97) return down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    if (code == -95 || code == -94) return down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
    return 0;
}

int MouseVk(int code) {
    if (code == -99) return VK_LBUTTON;
    if (code == -98) return VK_RBUTTON;
    if (code == -97) return VK_MBUTTON;
    if (code == -95) return VK_XBUTTON1;
    if (code == -94) return VK_XBUTTON2;
    return 0;
}

} // namespace

Bindings Resolve(HANDLE process) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    const DWORD pid = process ? GetProcessId(process) : 0;
    const ULONGLONG now = GetTickCount64();
    if (g_cache.pid != pid) g_cache = Cache{};
    g_cache.pid = pid;
    if (g_cache.checkedAt && now - g_cache.checkedAt < 2000) return g_cache.bindings;
    g_cache.checkedAt = now;

    WIN32_FILE_ATTRIBUTE_DATA info{};
    bool locate = g_cache.path.empty() || !FileInfo(g_cache.path, info);
    if (locate) {
        FILETIME newest{};
        g_cache.path = FindOptions(process, newest);
        g_cache.writeTime = {};
        if (!g_cache.path.empty()) FileInfo(g_cache.path, info);
    }
    if (!g_cache.path.empty() && FileInfo(g_cache.path, info) &&
        (!g_cache.bindings.loaded || CompareFileTime(&info.ftLastWriteTime, &g_cache.writeTime) != 0)) {
        Bindings parsed = ReadBindings(g_cache.path);
        if (parsed.loaded) {
            g_cache.bindings = parsed;
            g_cache.writeTime = info.ftLastWriteTime;
        }
    }
    return g_cache.bindings;
}

bool Send(int code, bool down) {
    if (code > 0) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        UINT scan = MapVirtualKeyW((UINT)code, MAPVK_VK_TO_VSC_EX);
        if (scan) {
            input.ki.wScan = (WORD)(scan & 0xffu);
            input.ki.dwFlags = KEYEVENTF_SCANCODE;
            if ((scan & 0xff00u) == 0xe000u) input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        } else {
            input.ki.wVk = (WORD)code;
        }
        if (!down) input.ki.dwFlags |= KEYEVENTF_KEYUP;
        return SendInput(1, &input, sizeof(input)) == 1;
    }
    const DWORD flag = MouseFlag(code, down);
    if (!flag) return false;
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    if (code == -95) input.mi.mouseData = XBUTTON1;
    if (code == -94) input.mi.mouseData = XBUTTON2;
    return SendInput(1, &input, sizeof(input)) == 1;
}

bool Down(int code) {
    const int vk = code > 0 ? code : MouseVk(code);
    return vk && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

} // namespace citron::gamebind
