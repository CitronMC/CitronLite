#include "citron/ui_script.h"
#include "ui/draw.h"
#include <algorithm>
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
    settle = kSettleFrames;   // the first steps name controls, so the panel has to have drawn them
    std::string text;
    while (std::getline(in, text)) {
        if (!text.empty() && text.back() == '\r') text.pop_back();
        if (text.empty() || text[0] == '#') continue;
        std::istringstream row(text);
        Line l;
        row >> l.op;
        const bool placed = l.op == "move" || l.op == "click" || l.op == "down";
        if (placed && (row >> std::ws).peek() == '@') {
            row >> l.target;
            l.target.erase(0, 1);
            l.a = l.b = 0.5f;
        }
        if (l.op == "type" || l.op == "shot" || l.op == "rects") {
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
        float x = l.a, y = l.b;
        if (!l.target.empty()) {
            ui::R r;
            if (!ui::ItemRect(l.target, r)) {
                if (std::find(missing.begin(), missing.end(), l.target) == missing.end()) missing.push_back(l.target);
                return false;
            }
            x = r.x + r.w * l.a;
            y = r.y + r.h * l.b;
        }
        pointer = MAKELPARAM((WORD)(short)std::lround(x * w.scale), (WORD)(short)std::lround(y * w.scale));
        pointing = true;
        SendMessageW(h, WM_MOUSEMOVE, 0, pointer);
        return true;
    };
    if (l.op == "move") {
        lost = !point();
    } else if (l.op == "down") {
        lost = !point();
        if (!lost) Button(h, l.button, true, pointer);
    } else if (l.op == "up") {
        if (!lost) Button(h, l.button, false, pointer);
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
    } else if (l.op == "rects") {
        std::ofstream out(l.text, std::ios::binary);
        for (const auto& [id, r] : ui::ItemRects()) out << r.x << ' ' << r.y << ' ' << r.w << ' ' << r.h << ' ' << id << '\n';
    }
    return true;
}

}  // namespace citron
