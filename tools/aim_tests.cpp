// Aim Assist's step: it turns toward the target, never past it, never faster than its ceiling.
#include "citron/aim_smooth.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using citron::AimStep;

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    const float dt = 1.f / 144.f;

    // the sign: the step goes against the error
    Require(AimStep(0.3f, dt, 0.6f, 0.4f, 0.5f) < 0.f, "a positive error turned the wrong way");
    Require(AimStep(-0.3f, dt, 0.6f, 0.4f, 0.5f) > 0.f, "a negative error turned the wrong way");
    Require(AimStep(0.f, dt, 1.f, 0.f, 0.5f) == 0.f, "no error, no step");

    // never past the target, whatever the frame time
    for (float err : {0.001f, 0.01f, 0.1f, 1.f, 3.f})
        for (float frame : {0.001f, 0.016f, 0.1f, 1.f})
            for (float rnd : {0.f, 0.5f, 0.999f}) {
                const float step = AimStep(err, frame, 1.f, 1.f, rnd);
                Require(std::fabs(step) <= err + 1e-6f, "a step went past the target");
                Require(std::fabs(step) <= 0.30f + 1e-6f, "a step was a flick");
            }

    // more strength turns faster, more smoothing ramps up later
    const float weak = std::fabs(AimStep(0.5f, dt, 0.2f, 0.4f, 0.5f));
    const float strong = std::fabs(AimStep(0.5f, dt, 0.9f, 0.4f, 0.5f));
    Require(strong > weak, "Strength did not speed the turn up");
    const float sharp = std::fabs(AimStep(0.05f, dt, 0.6f, 0.f, 0.5f));
    const float floaty = std::fabs(AimStep(0.05f, dt, 0.6f, 1.f, 0.5f));
    Require(sharp > floaty, "Smoothing did not soften a small error");

    // a far target is followed at the ceiling, not jumped to
    const float far = std::fabs(AimStep(2.5f, dt, 1.f, 0.f, 0.5f));
    Require(far < 0.2f, "a far target was jumped to");
    Require(std::fabs(far - 26.f * dt) < 0.02f, "the top speed is not 26 radians a second at full Strength");

    // the unevenness is gone at the target and only there
    const float a = AimStep(0.002f, dt, 1.f, 1.f, 0.f), b = AimStep(0.002f, dt, 1.f, 1.f, 0.999f);
    Require(std::fabs(a - b) < 1e-5f, "the speed wobbles while settled");
    const float c = AimStep(1.f, dt, 1.f, 1.f, 0.f), d = AimStep(1.f, dt, 1.f, 1.f, 0.999f);
    Require(std::fabs(c - d) > 1e-3f, "the speed never varies");

    // below full Strength the pull fades close in; at full Strength it does not
    const float soft = std::fabs(AimStep(0.004f, dt, 0.1f, 0.4f, 0.5f));
    const float hard = std::fabs(AimStep(0.004f, dt, 1.f, 0.4f, 0.5f));
    Require(soft < hard * 0.5f, "low Strength still locks onto the centre");

    // a steady loop settles on the target and stays there
    float angle = 1.2f;
    for (int i = 0; i < 1440; ++i) angle += AimStep(angle, dt, 0.7f, 0.3f, (float)((i * 7919) % 1000) / 1000.f);
    Require(std::fabs(angle) < 0.01f, "ten seconds of steps did not settle");

    std::cout << "Aim Assist step tests passed\n";
    return 0;
}
