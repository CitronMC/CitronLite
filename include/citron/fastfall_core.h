// Fast Fall's decisions, with no game and no clock in them so they can be tested.
//
// Nothing happens on the ground, on an elytra or on the way up: a fall counts once the vertical
// velocity is past -0.08 (standing still reads -0.0784). A fall that began with your own jump only
// counts with After Jumps. While it counts, the vertical velocity is set to -Fall Speed once a tick
// (3.92 a tick is the game's own terminal velocity) and the game's ticks run Timer Speed times as
// fast. The speed is a floor rather than a landing: the game's collision still lands you on whatever
// is in the way. Both are ranges; a fall rolls one number from each when it begins and keeps it.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace citron::fastfall {

struct Params {
    float speedLo = 1.5f, speedHi = 1.5f;   // blocks a tick a fall is brought down to; 0 leaves the fall's own speed alone
    float timerLo = 1.f, timerHi = 1.f;     // times the game's ticks run while it falls; 1 leaves the clock alone
    bool afterJump = false;                 // a fall that began with your own jump counts too
};

// A number from lo to hi, every thousandth of the way equally likely (both ends too): `u`, from 0 up to but not
// including 1, picks which. The ends may come either way round; a number that is not one gives 0 (nothing done).
inline float Roll(float lo, float hi, float u) {
    if (!std::isfinite(lo) || !std::isfinite(hi) || !std::isfinite(u)) return 0.f;
    if (hi < lo) std::swap(lo, hi);
    const long long a = std::llround(lo * 1000.f), b = std::llround(hi * 1000.f);
    const long long pick = a + (long long)std::floor(std::clamp(u, 0.f, 1.f) * (float)(b - a + 1));
    return (float)std::min(pick, b) / 1000.f;
}

struct Sample {
    bool motionOk = false;     // the player's movement was read and looks like one
    float pos[3] = {0.f, 0.f, 0.f};     // where it is
    float last[3] = {0.f, 0.f, 0.f};    // where it was a tick ago
    float vel[3] = {0.f, 0.f, 0.f};     // blocks a tick
    bool groundKnown = false, onGround = false;   // the game's on-ground flag
    bool glideKnown = false, gliding = false;     // the game's gliding flag
    bool jumpHeld = false;                        // the jump key is down
    float draw[2] = {0.5f, 0.5f};                 // two fresh numbers in [0, 1): what a fall that begins now rolls Fall Speed
                                                  // and Timer Speed from (read only when one does)
};

enum Event : unsigned {
    kFallStarted = 1u << 0,    // a fall began to be helped
};

struct Result {
    bool write = false;        // write `vy` over the vertical velocity
    float vy = 0.f;
    float timer = 1.f;         // the clock's factor wanted now: 1 is the game as shipped
    float speed = 0.f;         // the Fall Speed this fall rolled (0 outside one, or when it rolled none)
    unsigned events = 0;
};

constexpr float kTerminal = 3.92f;       // the most the game falls in a tick
constexpr float kPastTheTop = -0.08f;    // a fall counts from here down
constexpr float kJumpSpeed = 0.2f;       // leaving the ground this fast upward is a jump (or a launch)
constexpr float kMinTimer = 1.f, kMaxTimer = 5.f;
constexpr int kWritesPerTick = 4;        // the game's own end-of-tick update can undo a write: it is made again, this often

class Core {
public:
    unsigned falls = 0;        // falls helped, running, for the status line
    unsigned writes = 0;       // velocity writes asked for

    // Forget everything: another world, the module switched off.
    void Reset() {
        wasGround_ = true;
        jumped_ = false;
        acting_ = false;
        haveLast_ = false;
        tick_ = 0;
        tickWrites_ = 0;
        speed_ = 0.f;
        timer_ = 1.f;
    }

    // True while a fall is being helped: the clock may be running faster and the caller must put it back after.
    bool Acting() const { return acting_; }

    Result Step(const Params& p, const Sample& s) {
        Result r;
        // Without the movement, the ground flag or the gliding flag nothing is known: nothing is done. (Doing
        // nothing on a guess is the safe side: a fall helped on an elytra would end the flight.)
        if (!s.motionOk || !s.groundKnown || !s.glideKnown) {
            acting_ = false;
            haveLast_ = false;
            return r;
        }

        // A tick has passed when the position moved.
        if (haveLast_) {
            bool moved = false;
            for (int i = 0; i < 3; ++i) moved = moved || s.pos[i] != pos_[i] || s.last[i] != last_[i];
            if (moved) { ++tick_; tickWrites_ = 0; }
        }
        std::memcpy(pos_, s.pos, sizeof pos_);
        std::memcpy(last_, s.last, sizeof last_);
        haveLast_ = true;

        if (s.onGround) {
            wasGround_ = true;
            jumped_ = false;
            acting_ = false;
            return r;
        }
        if (wasGround_) {   // it has just left the ground: by a jump of its own?
            wasGround_ = false;
            jumped_ = s.jumpHeld && s.vel[1] > kJumpSpeed;
        }
        if ((jumped_ && !p.afterJump) || s.vel[1] > kPastTheTop || s.gliding) return r;

        if (!acting_) {
            acting_ = true;
            ++falls;
            speed_ = Roll(p.speedLo, p.speedHi, s.draw[0]);    // one roll for the whole fall
            timer_ = Roll(p.timerLo, p.timerHi, s.draw[1]);
            r.events |= kFallStarted;
        }
        r.speed = speed_;
        if (timer_ > kMinTimer + 0.0005f) r.timer = std::min(timer_, kMaxTimer);
        if (speed_ > 0.0005f) {
            const float target = -std::min(kTerminal, speed_);
            if (s.vel[1] > target + 0.001f && tickWrites_ < kWritesPerTick) {
                r.write = true;
                r.vy = target;
                ++tickWrites_;
                ++writes;
            }
        }
        return r;
    }

private:
    bool wasGround_ = true, jumped_ = false, acting_ = false, haveLast_ = false;
    float pos_[3] = {0.f, 0.f, 0.f}, last_[3] = {0.f, 0.f, 0.f};
    unsigned tick_ = 0;
    int tickWrites_ = 0;
    float speed_ = 0.f, timer_ = 1.f;   // what the fall being helped rolled
};

}  // namespace citron::fastfall
