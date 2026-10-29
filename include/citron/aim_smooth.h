// Aim Assist's step: how far the view turns toward the target in one frame.
//
// The step is a saturating curve of the error rather than a fraction of it, so the turn speed has a
// ceiling: a small error settles gently, a large one is closed at no more than the top speed, and a
// target appearing far off is followed rather than snapped to.
//
//   frac = 2 * sigmoid(|err| * k) - 1     0 at the target, toward 1 far from it
//   step = vmax * frac * dt               k from Smoothing, vmax from Strength
#pragma once
#include <cmath>

namespace citron {

// err: current minus target, radians. dt: seconds since the last step. s: Strength 0..1.
// sm: Smoothing 0..1. rnd: a fresh random number in [0, 1). Returns the step to add to the angle.
inline float AimStep(float err, float dt, float s, float sm, float rnd) {
    float ae = std::fabs(err);
    if (ae < 1e-6f) return 0.0f;

    float k    = 1.0f + (1.0f - sm) * 7.0f;
    float frac = 2.0f / (1.0f + std::exp(-ae * k)) - 1.0f;
    float vmax = 2.0f + s * 24.0f;                      // radians a second
    float step = vmax * frac * dt;

    // A little unevenness in the speed, gone as the aim settles.
    float w = sm * 0.12f;
    step *= 1.0f + (rnd * 2.0f - 1.0f) * w * frac;

    // Below full Strength the pull eases off close to the target instead of locking on its centre.
    float dead = (1.0f - s) * 0.011f;
    if (dead > 1e-5f && ae < 3.0f * dead) { float t = ae / (3.0f * dead); step *= t * t; }

    if (step > ae)    step = ae;                        // never past the target
    if (step > 0.30f) step = 0.30f;                     // and never a flick
    if (step < 0.0f)  step = 0.0f;
    return (err > 0.0f) ? -step : step;
}

} // namespace citron
