#include "../../backend/backend_impl.h"
#include <algorithm>
#include <cmath>

namespace citron {

void Backend::Impl::FinishTap() {
    if (!tapActive) {
        SetForwardHeldBack(false);
        return;
    }
    gamebind::Send(binds.back, false);
    SetForwardHeldBack(false);
    if (ForwardPhysical()) gamebind::Send(binds.forward, true);
    tapActive = false;
}

// After a hit lands: forward up and back down for a moment, then forward again if you still hold it.
void Backend::Impl::STapTick(const Modules& m) {
    const auto now = clock_t_::now();
    if (tapActive && now >= tapEnd) FinishTap();
    const ModuleSnapshot* s = Mod(m, "stap");
    if (!s || !s->on) {
        if (tapActive) FinishTap();
        tapSeenHit = landed.count;
        return;
    }
    if (landed.count == tapSeenHit) return;
    tapSeenHit = landed.count;
    if (tapActive || !landed.fresh || !InGame() || !ForwardPhysical() || PhysicalBindingDown(binds.back)) return;
    float lo = Num(*s, "range_min", 0.f), hi = Num(*s, "range_max", 3.f);
    if (hi < lo) std::swap(lo, hi);
    if (landed.distance < lo || landed.distance > hi) return;
    if (!ChanceAllows(*s)) return;
    if (Check(*s, "sprintOnly", false)) {
        bool sprinting = false;
        if (!local.valid || !rd.Sprinting(local.actor, sprinting) || !sprinting) return;
    }
    float holdLo = Num(*s, "hold_min", 60.f), holdHi = Num(*s, "hold_max", 110.f);
    if (holdHi < holdLo) std::swap(holdLo, holdHi);
    const int hold = (int)(holdLo + (holdHi - holdLo) * (float)tapRng.unit());
    SetForwardHeldBack(true);
    if (!gamebind::Send(binds.forward, false) || !gamebind::Send(binds.back, true)) {
        SetForwardHeldBack(false);
        gamebind::Send(binds.back, false);
        if (ForwardPhysical()) gamebind::Send(binds.forward, true);
        return;
    }
    tapActive = true;
    tapEnd = now + std::chrono::milliseconds(std::max(hold, 1));
}

void Backend::Impl::SnapTapTick(const Modules& m) {
    const ModuleSnapshot* s = Mod(m, "snaptap");
    int mode = 0;
    if (s && s->on && InGame()) {
        switch (Pick(*s, "keys", 0)) {
        case 1: mode = 2; break;
        case 2: mode = 3; break;
        default: mode = 1; break;
        }
    }
    SetSnapTap(mode);
}

void Backend::Impl::CancelJumpReset() {
    if (jumpDown && !PhysicalBindingDown(binds.jump)) gamebind::Send(binds.jump, false);
    jumpDown = jumpPending = false;
    hurtPrimed = false;
    jumpContext = false;
    jumpHitN = 0;
    jumpOpponent = 0;
}

// A jump the moment the game's hurt counter rises from a player's hit.
void Backend::Impl::JumpResetTick(const Modules& m) {
    const auto now = clock_t_::now();
    const ModuleSnapshot* s = Mod(m, "jumpreset");
    const bool wanted = s && s->on && InGame() && local.valid;
    const uintptr_t at = SiteAt(game::Site::HurtTime);
    const bool hookWanted = wanted || On(m, "velocity");
    if (hookWanted && !hurtHook.installed && at) engine::InstallCaptureRaxHook(proc, hurtHook, at, 5);
    if (!hookWanted && hurtHook.installed) {
        engine::RemoveHook(proc, hurtHook);
        hurtHook = engine::CaveHook{};
    }
    if (!wanted) {
        if (jumpContext || jumpDown) CancelJumpReset();
        return;
    }
    if (!jumpContext) {
        jumpContext = true;
        // a jump of ours that outlived the last context (death, a menu) goes up first
        if (jumpSent && !PhysicalBindingDown(binds.jump)) gamebind::Send(binds.jump, false);
        jumpSent = false;
        hurtPrimed = false;
    }
    if (jumpDown && now >= jumpUp) {
        if (!PhysicalBindingDown(binds.jump)) gamebind::Send(binds.jump, false);
        jumpDown = false;
    }
    if (jumpPending && now >= jumpFireAt && !jumpDown) {
        jumpPending = false;
        if (!PhysicalBindingDown(binds.jump) && gamebind::Send(binds.jump, true)) {
            jumpDown = jumpSent = true;
            jumpUp = now + std::chrono::milliseconds(40);
        }
    }
    uintptr_t hp = 0;
    int hurt = 0;
    if (!hurtHook.installed || !engine::Read(proc, hurtHook.slot, hp) || !rd.Plausible(hp) || !engine::Read(proc, hp, hurt) ||
        hurt < 0 || hurt > 200) {
        hurtPrimed = false;
        return;
    }
    if (!hurtPrimed) {
        prevHurt = hurt;
        hurtPrimed = true;
        return;
    }
    const bool hit = hurt > 0 && hurt > prevHurt;
    prevHurt = hurt;
    if (!hit || jumpDown || jumpPending) return;

    const float cone = Num(*s, "fov", 360.f);
    // Fire, lava, falls and mobs raise the counter too: only a player in reach counts.
    if (!AttackerInCone(cone, 6.5f, true, m) || !ChanceAllows(*s)) return;
    if (Check(*s, "hitTrade", false)) {
        if (now - jumpLastHit > std::chrono::seconds(2)) jumpHitN = 0;
        const uintptr_t opponent = landed.target && now - landed.at < std::chrono::milliseconds(2500) ? landed.target : 0;
        if (opponent && opponent != jumpOpponent) {
            jumpHitN = 0;
            jumpOpponent = opponent;
        }
        jumpLastHit = now;
        const int phase = ++jumpHitN & 3;
        if (phase != 2 && phase != 3) return;
    }
    if (Check(*s, "simple", false)) {
        if (!PhysicalBindingDown(binds.jump) && gamebind::Send(binds.jump, true)) {
            jumpDown = jumpSent = true;
            jumpUp = now + std::chrono::milliseconds(40);
        }
        return;
    }
    int lo = (int)Num(*s, "delay_min", 30.f), hi = (int)Num(*s, "delay_max", 80.f);
    if (hi < lo) std::swap(lo, hi);
    jumpPending = true;
    jumpFireAt = now + std::chrono::milliseconds(jumpRng.range(lo, hi + 1));
}

void Backend::Impl::NoJumpDelayTick(const Modules& m) {
    const uintptr_t at = SiteAt(game::Site::JumpDelay);
    if (!at) return;
    static const uint8_t kJne[2] = {0x0F, 0x85}, kSkip[2] = {0xEB, 0x04};
    const bool want = On(m, "nojumpdelay") && InGame();
    if (want && !jumpDelay.on) WritePatch(jumpDelay, at + 5, kSkip, 2, kJne);
    else if (!want && jumpDelay.on) ClearPatch(jumpDelay);
}

// Scales the knockback once, on the tick the hurt counter rises. 100% keeps the hit as it is.
void Backend::Impl::VelocityTick(const Modules& m) {
    const ModuleSnapshot* s = Mod(m, "velocity");
    if (!s || !s->on || !local.valid || !hurtHook.installed) {
        velPrimed = false;
        return;
    }
    uintptr_t hp = 0;
    int hurt = 0;
    if (!engine::Read(proc, hurtHook.slot, hp) || !rd.Plausible(hp) || !engine::Read(proc, hp, hurt) || hurt < 0 || hurt > 200) {
        velPrimed = false;
        return;
    }
    if (!velPrimed) {
        velPrevHurt = hurt;
        velPrimed = true;
        return;
    }
    const bool hit = hurt > 0 && hurt > velPrevHurt;
    velPrevHurt = hurt;
    const auto now = clock_t_::now();
    if (!hit || now - velLastApplied < std::chrono::milliseconds(150) || !ChanceAllows(*s)) return;
    auto roll = [&](const char* key, float def) {
        float lo = Num(*s, (std::string(key) + "_min").c_str(), def), hi = Num(*s, (std::string(key) + "_max").c_str(), def);
        if (hi < lo) std::swap(lo, hi);
        return (lo + (hi - lo) * (float)velRng.unit()) / 100.f;
    };
    const float kh = roll("horizontal", 100.f), kv = roll("vertical", 100.f);
    bedrock::Motion mo;
    if (!rd.ReadMotion(local.actor.ptr, mo)) return;
    const uintptr_t vel = mo.state + game::kStateVelocity;
    engine::Write(proc, vel, mo.vel[0] * kh);
    engine::Write(proc, vel + 8, mo.vel[2] * kh);
    if (mo.vel[1] > 0.f) engine::Write(proc, vel + 4, mo.vel[1] * kv);
    velLastApplied = now;
}

// A factor on the air acceleration the game computes, so sprinting and walking both keep their own.
void Backend::Impl::AirAccelTick(const Modules& m) {
    const ModuleSnapshot* s = Mod(m, "airaccel");
    const uintptr_t at = SiteAt(game::Site::AirAccel);
    if (!s || !s->on || !at) {
        if (airHook.installed) {
            engine::RemoveHook(proc, airHook);
            airHook = engine::CaveHook{};
        }
        airWrote = -1.f;
        return;
    }
    const float factor = std::clamp(Num(*s, "multiplier", 100.f), 10.f, 400.f) / 100.f;
    if (!airHook.installed) {
        if (engine::InstallXmm0ScaleHook(proc, airHook, at + 1, 6, factor)) airWrote = factor;
        return;
    }
    if (std::fabs(factor - airWrote) > 1e-6f && engine::Write(proc, airHook.slot, factor)) airWrote = factor;
}

void Backend::Impl::ReleaseSprint() {
    if (sprintHeld && !PhysicalBindingDown(sprintCode)) gamebind::Send(sprintCode, false);
    sprintHeld = false;
    sprintCode = 0;
    sprintRepress = sprintReleased = sprintStalled = false;
}

// Holds the game's sprint binding while you walk forward, and presses it again after a hit or a stop
// so the sprint comes back.
void Backend::Impl::AutoSprintTick(const Modules& m) {
    const ModuleSnapshot* s = Mod(m, "autosprint");
    const bool on = s && s->on && InGame();
    const bool strafe = s && Check(*s, "strafe", true);
    const bool moving = PhysicalBindingDown(binds.forward) ||
                        (strafe && (PhysicalBindingDown(binds.left) || PhysicalBindingDown(binds.right)));
    const bool want = on && moving;
    if (sprintHeld && sprintCode != binds.sprint) ReleaseSprint();
    if (!want) {
        if (sprintHeld) ReleaseSprint();
        return;
    }
    if (!sprintHeld) {
        if (gamebind::Send(binds.sprint, true)) {
            sprintHeld = true;
            sprintCode = binds.sprint;
        }
        return;
    }
    const auto now = clock_t_::now();
    auto again = [&](int ms) {
        if (sprintRepress) return;
        sprintRepress = true;
        sprintReleased = false;
        sprintRepressAt = now + std::chrono::milliseconds(ms);
    };
    if (landed.fresh) again(45);
    if (local.valid) {
        const float* v = local.motion.vel;
        const float speed = std::sqrt(v[0] * v[0] + v[2] * v[2]);
        if (speed < 0.04f) sprintStalled = true;
        else if (sprintStalled && speed > 0.09f) {
            sprintStalled = false;
            again(25);
        }
    }
    if (!sprintRepress || now < sprintRepressAt) return;
    if (PhysicalBindingDown(sprintCode)) {
        sprintRepress = sprintReleased = false;
        return;
    }
    if (!sprintReleased) {
        gamebind::Send(sprintCode, false);
        sprintReleased = true;
        sprintRepressAt = now + std::chrono::milliseconds(20);
        return;
    }
    gamebind::Send(sprintCode, true);
    sprintRepress = sprintReleased = false;
}

}  // namespace citron
