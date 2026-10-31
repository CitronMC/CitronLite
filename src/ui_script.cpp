#include "citron/ui_script.h"
#include <cmath>
#include <fstream>
#include <sstream>

namespace citron {
namespace {

constexpr int kSettleFrames = 3;   // for ImGui to take in one event and answer it

void Button(HWND h, int button, bool down, LPARAM at) {
    static const UINT kDown[3] = {WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN};
    static const UINT kUp[3] = {WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP};
    if (button < 3) SendMessageW(h, down ? kDown[button] : kUp[button], 0, at);
    else SendMessageW(h, down ? WM_XBUTTONDOWN : WM_XBUTTONUP, MAKEWPARAM(0, button == 3 ? XBUTTON1 : XBUTTON2), at);
}

}  // namespace

bool UiScript::Load(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream in(path);
    if (!in) return false;
    std::string text;
    while (std::getline(in, text)) {
        if (!text.empty() && text.back() == '\r') text.pop_back();
        if (text.empty() || text[0] == '#') continue;
        std::istringstream row(text);
        Line l;
        row >> l.op;
        if (l.op == "type" || l.op == "shot") {
            std::getline(row >> std::ws, l.text);
            lines.push_back(l);
        } else if (l.op == "click") {
            row >> l.a >> l.b >> l.button;
            l.op = "move";
            lines.push_back(l);
            l.op = "down";
            lines.push_back(l);
            l.op = "up";
            lines.push_back(l);
        } else if (l.op == "key") {
            row >> l.a;
            l.op = "keydown";
            lines.push_back(l);
            l.op = "keyup";
            lines.push_back(l);
        } else if (l.op == "up") {
            row >> l.button;
            lines.push_back(l);
        } else {
            row >> l.a >> l.b >> l.button;
            lines.push_back(l);
        }
    }
    return true;
}

bool UiScript::Step(platform::Window& w, std::string& capture) {
    const HWND h = w.hwnd;
    // The real pointer is elsewhere, so the system answers every move with a leave. Saying it again
    // each frame keeps ImGui's pointer where the script put it.
    if (pointing) SendMessageW(h, WM_MOUSEMOVE, 0, pointer);
    if (settle > 0) {
        --settle;
        return true;
    }
    if (GetTickCount64() < until) return true;
    if (next >= lines.size()) return false;
    const Line& l = lines[next++];
    settle = kSettleFrames;

    auto point = [&] {
        pointer = MAKELPARAM((WORD)(short)std::lround(l.a * w.scale), (WORD)(short)std::lround(l.b * w.scale));
        pointing = true;
        SendMessageW(h, WM_MOUSEMOVE, 0, pointer);
    };
    if (l.op == "move") {
        point();
    } else if (l.op == "down") {
        point();
        Button(h, l.button, true, pointer);
    } else if (l.op == "up") {
        Button(h, l.button, false, pointer);
    } else if (l.op == "wheel") {
        SendMessageW(h, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)((int)l.a * WHEEL_DELTA)), pointer);
    } else if (l.op == "keydown" || l.op == "keyup") {
        const UINT vk = (UINT)l.a, scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        SendMessageW(h, l.op == "keydown" ? WM_KEYDOWN : WM_KEYUP, vk, (LPARAM)(1u | (scan << 16)));
    } else if (l.op == "type") {
        for (char c : l.text) SendMessageW(h, WM_CHAR, (WPARAM)(unsigned char)c, 1);
    } else if (l.op == "wait") {
        until = GetTickCount64() + (unsigned long long)l.a;
    } else if (l.op == "shot") {
        capture = l.text;
    }
    return true;
}

}  // namespace citron
