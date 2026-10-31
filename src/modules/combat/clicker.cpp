#include "../../backend/backend_impl.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace citron {

void Backend::Impl::SendAttack(bool down) {
    if (down) CountAttack();
    gamebind::Send(binds.attack, down);
}

void Backend::Impl::DriveButton(BtnClk& st, HumanDelay& hd, Xrng& rng, bool want, int code, float mn, float mx, bool hold) {
    const auto now = clock_t_::now();
    auto press = [&](bool down) {
        if (code == binds.attack) SendAttack(down);
        else gamebind::Send(code, down);
    };
    if (!want) {
        if (st.down) press(false);
        st.down = st.active = false;
        return;
    }
    if (!st.active) {
        st.active = true;
        st.down = false;
        st.count = 0;
        st.next = now;
    }
    if (hold) {   // breaking a block: keep it held
        if (!st.down) {
            press(true);
            st.down = true;
        }
        return;
    }
    if (now < st.next) return;
    if (now - st.next > std::chrono::milliseconds(60)) st.next = now;
    if (st.down) {
        press(false);
        st.down = false;
        ++st.count;
        st.next += std::chrono::microseconds((long long)(st.curUp * 1000.0));
    } else {
        const HumanDelay::Pair d = hd.get(mn, mx, rng);
        st.curUp = d.up;
        press(true);
        st.down = true;
        st.next += std::chrono::microseconds((long long)(d.down * 1000.0));
    }
}

// Clicks for you while you hold the game's attack (Auto Clicker) or use (Right Clicker) binding,
// whatever they are bound to.
void Backend::Impl::ClickerTick(const Modules& m) {
    const ModuleSnapshot* c = Mod(m, "clicker");
    const ModuleSnapshot* r = Mod(m, "rightclicker");
    const bool left = c && c->on, right = r && r->on;
    const bool focused = GameFocused() && throwing.stage == 0 && !guiOpen.load(std::memory_order_relaxed);
    const bool leftGate = left && focused && (!Check(*c, "avoidMenus", true) || CursorHidden());
    const bool rightGate = right && focused && CursorHidden();

    auto cps = [](const ModuleSnapshot* s, float lo, float hi, float& mn, float& mx) {
        mn = s ? Num(*s, "cps_min", lo) : lo;
        mx = s ? Num(*s, "cps_max", hi) : hi;
        if (mx < mn) std::swap(mn, mx);
        mn = std::max(mn, 1.f);
        mx = std::max(mx, 1.f);
    };
    float lmn, lmx, rmn, rmx;
    cps(c, 10.f, 14.f, lmn, lmx);
    cps(r, 8.f, 12.f, rmn, rmx);
    const float ljit = c ? Num(*c, "jitter", 3.f) : 0.f, rjit = r ? Num(*r, "jitter", 0.f) : 0.f;

    const bool onBlock = lookBlock || clock_t_::now() - lookBlockAt < std::chrono::milliseconds(180);
    const bool breaking = left && Check(*c, "breakBlock", true) && onBlock;
    const bool wantL = leftGate && PhysicalBindingDown(binds.attack);
    const bool wantR = rightGate && PhysicalBindingDown(binds.use);
    DriveButton(leftClk, leftHd, leftRng, wantL, binds.attack, lmn, lmx, breaking);
    DriveButton(rightClk, rightHd, rightRng, wantR, binds.use, rmn, rmx, false);
    const bool gate = wantL || wantR;

    const bool clicking = (wantL && !breaking) || wantR;
    const int intensity = (wantL && !breaking) ? (int)std::lround(ljit) : wantR ? (int)std::lround(rjit) : 0;
    const auto now = clock_t_::now();
    if (gate && clicking && intensity > 0) {
        if (now >= jitterNext) {
            jitterNext = now + std::chrono::milliseconds(10);
            int dx = 0, dy = 0;
            if (clickJit.next(intensity, jitRng, dx, dy)) {
                INPUT in = {};
                in.type = INPUT_MOUSE;
                in.mi.dwFlags = MOUSEEVENTF_MOVE;
                in.mi.dx = dx;
                in.mi.dy = dy;
                SendInput(1, &in, sizeof(INPUT));
            }
        }
    } else {
        clickJit.reset();
    }
}

}  // namespace citron
