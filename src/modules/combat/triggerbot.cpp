#include "../../backend/backend_impl.h"
#include <algorithm>

namespace citron {

// Hits whoever is under your crosshair within reach, at a human rate, after a reaction delay.
void Backend::Impl::TriggerBotTick(const Modules& m) {
    const ModuleSnapshot* s = Mod(m, "triggerbot");
    const auto now = clock_t_::now();
    if (trig.down && now >= trig.releaseAt) {
        SendAttack(false);
        trig.down = false;
    }
    auto lose = [&] { trig.target = 0; };
    if (!s || !s->on || !local.valid || !look.valid || !InGame()) return lose();
    // Holding attack yourself: the Auto Clicker or your own hand has it.
    if (PhysicalBindingDown(binds.attack)) return lose();
    if (Check(*s, "onlyWeapon", false) && !HoldingWeapon()) return lose();
    // The game's own pick says a block is first in line: a click would hit the block.
    if (lookKnown && !lookEntity) return lose();
    const Ent* e = UnderCrosshair(curReach, m, Check(*s, "playersOnly", true));
    if (!e) return lose();
    if (e->actor != trig.target) {
        trig.target = e->actor;
        trig.seenAt = now;
        float lo = Num(*s, "delay_min", 0.f), hi = Num(*s, "delay_max", 0.f);
        if (hi < lo) std::swap(lo, hi);
        trig.delayMs = (int)(lo + (hi - lo) * (float)trigRng.unit());
        if (trig.nextClick < now) trig.nextClick = now;
    }
    if (now - trig.seenAt < std::chrono::milliseconds(trig.delayMs) || trig.down || now < trig.nextClick) return;
    float lo = Num(*s, "cps_min", 8.f), hi = Num(*s, "cps_max", 12.f);
    if (hi < lo) std::swap(lo, hi);
    const HumanDelay::Pair d = trigHd.get(lo, hi, trigRng);
    SendAttack(true);
    trig.down = true;
    trig.releaseAt = now + std::chrono::microseconds((long long)(d.down * 1000.0));
    trig.nextClick = now + std::chrono::microseconds((long long)((d.down + d.up) * 1000.0));
}

}  // namespace citron
