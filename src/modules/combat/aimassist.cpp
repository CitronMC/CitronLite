#include "../../backend/backend_impl.h"
#include "citron/aim_smooth.h"
#include <algorithm>
#include <cmath>

namespace citron {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = 57.2957795f;

float WrapPi(float a) {
    a = std::fmod(a + kPi, 2.f * kPi);
    if (a < 0.f) a += 2.f * kPi;
    return a - kPi;
}

float BoxDistance(const float (&eye)[3], const Ent& e) {
    float d2 = 0.f;
    for (int k = 0; k < 3; ++k) {
        const float c = std::clamp(eye[k], e.mn[k], e.mx[k]) - eye[k];
        d2 += c * c;
    }
    return std::sqrt(d2);
}

// How far you would turn across to face the middle of the box, in radians.
float Across(const float (&eye)[3], float yaw, const Ent& e) {
    const float cx = (e.mn[0] + e.mx[0]) * 0.5f, cz = (e.mn[2] + e.mx[2]) * 0.5f;
    return std::fabs(WrapPi(yaw - std::atan2(eye[0] - cx, eye[2] - cz)));
}

}  // namespace

// Your camera eased onto the player nearest your crosshair. The step is a saturating sigmoid of the
// angle (gentle as it settles, capped in speed far out), the point drifts slowly round the target so
// the crosshair floats on them, a new target is eased in over 150 ms, and turning away from them
// hands the aim back over your first three degrees of it.
void Backend::Impl::AimAssistTick(const Modules& m) {
    const ModuleSnapshot* s = Mod(m, "aimassist");
    const auto now = clock_t_::now();
    auto stand = [&] {
        aimTarget = 0;
        aimLastStep = {};
    };
    if (!s || !s->on || !local.valid || !look.valid || !InGame()) {
        stand();
        aimHaveYaw = false;
        return;
    }

    // Your own turning: whatever moved the camera since it was last seen, that was not this module.
    // The step's time is capped at a frame or so, so a stall of this thread never comes out as a flick.
    const float dt = aimLastStep == clock_t_::time_point{} ? 1.f / 240.f
                   : std::clamp(std::chrono::duration<float>(now - aimLastStep).count(), 0.f, 0.016f);
    const float turn = aimHaveYaw ? WrapPi(look.yaw - aimLastYaw) : 0.f;
    aimHaveYaw = true;
    aimLastYaw = look.yaw;
    aimHand = aimHand * std::exp(-dt / 0.04f) + turn;

    if (Check(*s, "onlyLmb", true) && !PhysicalBindingDown(binds.attack)) return stand();
    if (Check(*s, "onlyWeapon", false) && !HoldingWeapon()) return stand();

    const float strength = std::clamp(Num(*s, "strength", 55.f) / 100.f, 0.05f, 1.f);
    const float smooth = std::clamp(Num(*s, "smoothing", 40.f) / 100.f, 0.f, 1.f);
    const float range = Num(*s, "distance", 4.5f);
    const float cone = std::clamp(Num(*s, "fov", 45.f), 1.f, 180.f) / kDeg;
    const int part = Pick(*s, "aimpart", 0);
    const bool playersOnly = Check(*s, "playersOnly", true);
    const bool horizontalOnly = Check(*s, "horizontalOnly", false);

    // Whoever is nearest the crosshair across, kept until somebody is clearly better.
    Ent* best = nullptr;
    float bestKey = 1e9f, keptKey = -1.f;
    for (auto& kv : ents) {
        Ent& e = kv.second;
        if (!Fightable(e, m, playersOnly) || BoxDistance(local.eye, e) > range) continue;
        const float off = Across(local.eye, look.yaw, e);
        if (off > cone) continue;
        if (e.actor == aimTarget) keptKey = off;
        if (off < bestKey) {
            bestKey = off;
            best = &e;
        }
    }
    if (best && keptKey >= 0.f && best->actor != aimTarget && bestKey > keptKey * 0.75f) best = &ents[aimTarget];
    if (!best) return stand();
    if (best->actor != aimTarget) {
        aimTarget = best->actor;
        aimAcquiredAt = now;
    }
    if (!RefreshEnt(*best)) return stand();
    const Ent& t = *best;

    // Where on the box: a little inside its nearest point, the face, or the middle.
    float point[3];
    const float centre[3] = {(t.mn[0] + t.mx[0]) * 0.5f, (t.mn[1] + t.mx[1]) * 0.5f, (t.mn[2] + t.mx[2]) * 0.5f};
    if (part == 1) {
        point[0] = centre[0];
        point[1] = t.mn[1] + (t.mx[1] - t.mn[1]) * 0.9f;
        point[2] = centre[2];
    } else if (part == 2) {
        std::copy(centre, centre + 3, point);
    } else {
        for (int k = 0; k < 3; ++k) {
            const float nearest = std::clamp(local.eye[k], t.mn[k], t.mx[k]);
            point[k] = nearest + (centre[k] - nearest) * 0.34f;
        }
    }
    const float dx = local.eye[0] - point[0], dy = local.eye[1] - point[1], dz = local.eye[2] - point[2];
    const float horiz = std::sqrt(dx * dx + dz * dz);
    if (horiz < 1e-3f) return;
    const float rawErrYaw = WrapPi(look.yaw - std::atan2(dx, dz));
    float errYaw = rawErrYaw;
    float errPitch = WrapPi(look.pitch + std::atan2(dy, horiz));

    // the point drifts round the target, a bounded random walk wider with Smoothing
    {
        const float a = std::exp(-dt / 0.6f), amp = 0.006f + smooth * 0.016f;
        const float kick = amp * std::sqrt(1.f - a * a) * 1.732f;
        aimDriftYaw = aimDriftYaw * a + ((float)aimRng.unit() * 2.f - 1.f) * kick;
        aimDriftPitch = aimDriftPitch * a + ((float)aimRng.unit() * 2.f - 1.f) * kick;
        errYaw = WrapPi(errYaw + aimDriftYaw);
        errPitch += aimDriftPitch;
    }

    const float acquire = std::clamp(std::chrono::duration<float>(now - aimAcquiredAt).count() / 0.15f, 0.2f, 1.f);
    float sy = AimStep(errYaw, dt, strength, smooth, (float)aimRng.unit()) * acquire;
    float sp = horizontalOnly ? 0.f : AimStep(errPitch, dt, strength, smooth, (float)aimRng.unit()) * acquire;
    aimLastStep = now;

    // Close in, a little of their movement swings the heading round: the yaw fades out.
    if (horiz < 0.35f) sy *= horiz / 0.35f;
    // Turning away from them: the pull is gone over the first three degrees of it.
    if (aimHand != 0.f && (aimHand > 0.f) == (rawErrYaw > 0.f)) {
        const float left = 1.f - std::clamp(std::fabs(aimHand) * kDeg / 3.f, 0.f, 1.f);
        sy *= left * left;
        sp *= left * left;
    }
    if (std::fabs(sy) < 1e-6f && std::fabs(sp) < 1e-6f) return;

    const float yaw = look.yaw + sy;
    const float pitch = std::clamp(look.pitch + sp, -1.5607f, 1.5607f);
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) return;
    engine::Write(proc, look.camera + 0, yaw);
    if (sp != 0.f) engine::Write(proc, look.camera + 4, pitch);
    aimLastYaw = yaw;
}

}  // namespace citron
