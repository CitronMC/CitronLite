#include "../../backend/backend_impl.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace citron {
namespace {
constexpr float kShippedTps = 20.f;
}

void Backend::Impl::ReleaseClock() {
    if (!clockOurs) return;
    clockOurs = false;
    float tps = 0.f;
    // Only our own number goes back to 20: one somebody set since is theirs.
    if (timerAt && engine::Read(proc, timerAt, tps) && std::fabs(tps - clockSet) < 0.01f) engine::Write(proc, timerAt, kShippedTps);
}

// The game's ticks a second: Timer's speed, times Fast Fall's while it helps a fall.
void Backend::Impl::ClockTick(const Modules& m) {
    const ModuleSnapshot* t = Mod(m, "timer");
    const float base = t && t->on ? std::clamp(Num(*t, "ticks", 20.f), 1.f, 100.f) : kShippedTps;
    const float want = base * (fallActing ? fallFactor : 1.f);
    if (std::fabs(want - kShippedTps) < 1e-3f) {
        ReleaseClock();
        return;
    }
    if (!timerAt) return;
    const auto now = clock_t_::now();
    if (clockOurs && std::fabs(want - clockSet) < 1e-4f && now < clockCheck) return;
    clockCheck = now + std::chrono::milliseconds(100);
    float tps = 0.f;
    if (!engine::Read(proc, timerAt, tps)) return;
    if (std::fabs(tps - want) > 1e-4f) {
        // a clock somebody else is running is left to them
        if (!clockOurs && std::fabs(tps - kShippedTps) > 0.01f) return;
        if (!engine::Write(proc, timerAt, want)) return;
    }
    clockOurs = true;
    clockSet = want;
}

void Backend::Impl::FastFallTick(const Modules& m) {
    const ModuleSnapshot* s = Mod(m, "fastfall");
    if (!s || !s->on || !local.valid) {
        fall.Reset();
        fallActing = false;
        fallFactor = 1.f;
        return;
    }
    const auto now = clock_t_::now();
    if (now >= fallFlagsAt) {
        fallFlagsAt = now + std::chrono::milliseconds(8);
        groundOk = rd.OnGround(local.actor, onGround);
        glideOk = rd.Gliding(local.actor, gliding);
    }
    auto draw = [&] {
        fallRng ^= fallRng >> 12;
        fallRng ^= fallRng << 25;
        fallRng ^= fallRng >> 27;
        return (float)((fallRng * 2685821657736338717ull) >> 40) * (1.f / 16777216.f);
    };
    fastfall::Sample smp;
    smp.motionOk = true;
    std::memcpy(smp.pos, local.motion.pos, sizeof smp.pos);
    std::memcpy(smp.last, local.motion.last, sizeof smp.last);
    std::memcpy(smp.vel, local.motion.vel, sizeof smp.vel);
    smp.groundKnown = groundOk;
    smp.onGround = onGround;
    smp.glideKnown = glideOk;
    smp.gliding = gliding;
    smp.jumpHeld = PhysicalBindingDown(binds.jump);
    smp.draw[0] = draw();
    smp.draw[1] = draw();
    fastfall::Params p;
    p.speedLo = Num(*s, "speed_min", 1.4f);
    p.speedHi = Num(*s, "speed_max", 1.6f);
    p.timerLo = Num(*s, "timer_min", 1.f);
    p.timerHi = Num(*s, "timer_max", 1.f);
    p.afterJump = Check(*s, "afterJump", false);
    const fastfall::Result r = fall.Step(p, smp);
    if (r.write) engine::Write(proc, local.motion.state + game::kStateVelocity + 4, r.vy);
    fallActing = fall.Acting() && r.timer > 1.0005f;
    fallFactor = r.timer;
}

}  // namespace citron
