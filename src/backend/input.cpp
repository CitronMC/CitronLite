#include "backend_impl.h"
#include <windows.h>

namespace citron {
namespace {

std::atomic<bool> g_key[256]{};
std::atomic<uint32_t> g_edges[256]{};
std::atomic<uint32_t> g_attacks{0};

std::atomic<int> g_attackCode{-99};
std::atomic<int> g_forward{'W'}, g_back{'S'}, g_left{'A'}, g_right{'D'};

std::atomic<bool> g_wheelOwned{false};
std::atomic<unsigned long> g_wheelPid{0};
std::atomic<int> g_notches{0};

std::atomic<bool> g_forwardHeldBack{false};

// SnapTap. Only the hook thread touches the pairs.
std::atomic<int> g_snapMode{0};
struct Pair {
    int a = 0, b = 0;          // the two keys
    bool downA = false, downB = false;
    int muted = 0;             // the key we released for the game while it is still held
};
Pair g_pairs[2];

int MouseCode(int vk) {
    switch (vk) {
    case VK_LBUTTON: return -99;
    case VK_RBUTTON: return -98;
    case VK_MBUTTON: return -97;
    case VK_XBUTTON1: return -95;
    case VK_XBUTTON2: return -94;
    default: return 0;
    }
}

void Send(int vk, bool down) { gamebind::Send(vk, down); }

// Physical press or release of one key of a SnapTap pair. Returns true when the event must not reach
// the game: the auto-repeat of a key we released for it.
bool SnapEvent(Pair& p, int vk, bool down, bool repeat) {
    const bool isA = vk == p.a;
    if (repeat) return down && p.muted == vk;
    if (isA) p.downA = down; else p.downB = down;
    const int other = isA ? p.b : p.a;
    const bool otherDown = isA ? p.downB : p.downA;
    if (down) {
        if (otherDown && p.muted != other) {
            Send(other, false);
            p.muted = other;
        }
        if (p.muted == vk) p.muted = 0;
    } else {
        if (p.muted == other && otherDown) {
            Send(other, true);
            p.muted = 0;
        }
        if (p.muted == vk) p.muted = 0;
    }
    return false;
}

void ReleaseSnap() {
    for (Pair& p : g_pairs) {
        if (p.muted && ((p.muted == p.a && p.downA) || (p.muted == p.b && p.downB))) Send(p.muted, true);
        p = Pair{};
    }
}

LRESULT CALLBACK MouseProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const auto* hs = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
        const bool injected = (hs->flags & LLMHF_INJECTED) != 0;
        int vk = 0;
        bool down = false, ours = false;
        switch (wparam) {
        // The clicker sends left and right itself, so an injected one there is ours.
        case WM_LBUTTONDOWN: vk = VK_LBUTTON; down = true; ours = injected; break;
        case WM_LBUTTONUP: vk = VK_LBUTTON; ours = injected; break;
        case WM_RBUTTONDOWN: vk = VK_RBUTTON; down = true; ours = injected; break;
        case WM_RBUTTONUP: vk = VK_RBUTTON; ours = injected; break;
        // Middle and side buttons are never sent by this client. Mouse software that remaps them
        // re-injects them, and those still count as the player's.
        case WM_MBUTTONDOWN: vk = VK_MBUTTON; down = true; break;
        case WM_MBUTTONUP: vk = VK_MBUTTON; break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            vk = HIWORD(hs->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
            down = wparam == WM_XBUTTONDOWN;
            break;
        default: break;
        }
        if (vk && !ours) {
            const bool was = g_key[vk].exchange(down, std::memory_order_relaxed);
            if (down && !was) {
                g_edges[vk].fetch_add(1u, std::memory_order_relaxed);
                if (MouseCode(vk) == g_attackCode.load(std::memory_order_relaxed)) g_attacks.fetch_add(1u);
            }
        }
        if (wparam == WM_MOUSEWHEEL && g_wheelOwned.load(std::memory_order_acquire)) {
            DWORD owner = 0;
            if (HWND fg = GetForegroundWindow()) GetWindowThreadProcessId(fg, &owner);
            if (owner && owner == g_wheelPid.load(std::memory_order_relaxed)) {
                const int delta = (int)GET_WHEEL_DELTA_WPARAM(hs->mouseData);
                if (delta > 0) g_notches.fetch_add(1);
                else if (delta < 0) g_notches.fetch_sub(1);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const auto* ks = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        const int vk = (int)ks->vkCode;
        const bool injected = (ks->flags & LLKHF_INJECTED) != 0;
        if (vk > 0 && vk < 256 && !injected) {
            const bool down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
            const bool was = g_key[vk].exchange(down, std::memory_order_relaxed);
            const bool repeat = down && was;
            if (down && !was) {
                g_edges[vk].fetch_add(1u, std::memory_order_relaxed);
                if (vk == g_attackCode.load(std::memory_order_relaxed)) g_attacks.fetch_add(1u);
            }
            if (repeat && vk == g_forward.load(std::memory_order_relaxed) &&
                g_forwardHeldBack.load(std::memory_order_relaxed))
                return 1;
            const int mode = g_snapMode.load(std::memory_order_acquire);
            if (mode) {
                const int fw = g_forward.load(std::memory_order_relaxed), bk = g_back.load(std::memory_order_relaxed);
                const int lf = g_left.load(std::memory_order_relaxed), rt = g_right.load(std::memory_order_relaxed);
                Pair* p = nullptr;
                if ((mode == 1 || mode == 2) && (vk == fw || vk == bk)) {
                    p = &g_pairs[0];
                    p->a = fw; p->b = bk;
                } else if ((mode == 1 || mode == 3) && (vk == lf || vk == rt)) {
                    p = &g_pairs[1];
                    p->a = lf; p->b = rt;
                }
                if (p && SnapEvent(*p, vk, down, repeat)) return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

}  // namespace

bool PhysKeyDown(int vk) { return vk > 0 && vk < 256 && g_key[vk].load(std::memory_order_relaxed); }
uint32_t PhysKeyEdges(int vk) { return vk > 0 && vk < 256 ? g_edges[vk].load(std::memory_order_relaxed) : 0u; }

void ResetPhysicalInput() {
    for (auto& k : g_key) k.store(false, std::memory_order_relaxed);
}

void SetHookBindings(const gamebind::Bindings& b) {
    g_attackCode.store(b.attack, std::memory_order_relaxed);
    g_forward.store(b.forward, std::memory_order_relaxed);
    g_back.store(b.back, std::memory_order_relaxed);
    g_left.store(b.left, std::memory_order_relaxed);
    g_right.store(b.right, std::memory_order_relaxed);
}

uint32_t AttackPresses() { return g_attacks.load(std::memory_order_acquire); }
void CountAttack() { g_attacks.fetch_add(1u); }

void SetSnapTap(int mode) {
    const int was = g_snapMode.exchange(mode, std::memory_order_acq_rel);
    // The pairs belong to the hook thread; one that was holding a key back hands it over here only
    // when SnapTap goes off entirely, which the hook cannot be in the middle of for long.
    if (was && !mode) ReleaseSnap();
}

void SetForwardHeldBack(bool on) { g_forwardHeldBack.store(on, std::memory_order_relaxed); }
bool ForwardPhysical() { return PhysKeyDown(g_forward.load(std::memory_order_relaxed)); }

void SetWheelOwned(bool owned, unsigned long gamePid) {
    g_wheelPid.store(gamePid, std::memory_order_relaxed);
    if (!owned) g_notches.store(0, std::memory_order_relaxed);
    g_wheelOwned.store(owned, std::memory_order_release);
}

int TakeWheelNotches() { return g_notches.exchange(0, std::memory_order_acq_rel); }

void Backend::Impl::HookThread() {
    MSG msg{};
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    HHOOK mouse = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    HHOOK keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);
    hookTid.store(GetCurrentThreadId());
    {
        std::lock_guard<std::mutex> lock(hookStartMtx);
        hookStarted = true;
    }
    hookStartCv.notify_all();
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (mouse) UnhookWindowsHookEx(mouse);
    if (keyboard) UnhookWindowsHookEx(keyboard);
    hookTid.store(0);
}

}  // namespace citron
