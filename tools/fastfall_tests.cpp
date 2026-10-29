// Fast Fall's decisions against a small imitation of the game's fall.
//   ./build_mingw.sh tests
//
// The imitation steps in milliseconds. The game ticks every 50 ms (shorter while the clock runs faster): the
// position moves by the velocity, a floor at y = 0 stops it, then the velocity takes the game's gravity step
// ((v - 0.08) * 0.98). The module is asked once a millisecond, as the worker does, and what it writes goes
// straight back into the imitation.
#include "citron/fastfall_core.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

using namespace citron::fastfall;

namespace {

int g_failed = 0, g_checks = 0;
#define CHECK(cond, ...)                                                                       \
    do {                                                                                       \
        ++g_checks;                                                                            \
        if (!(cond)) {                                                                         \
            ++g_failed;                                                                        \
            std::printf("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond);                      \
            std::printf(__VA_ARGS__);                                                          \
            std::printf("\n");                                                                 \
        }                                                                                      \
    } while (0)

struct Game {
    float pos[3] = {0.f, 0.f, 0.f}, last[3] = {0.f, 0.f, 0.f}, vel[3] = {0.f, -0.0784f, 0.f};
    bool onGround = true, gliding = false, jumpHeld = false;
    bool groundKnown = true, glideKnown = true;
    double ms = 0.0, nextTick = 25.0;
    float clock = 1.f;                 // what the module asked the game's clock to run at
    bool lose = false;                 // the game undoes every other write (the race)
    bool loseAll = false;              // the game undoes every write
    Core core;
    Params p;
    float draw[2] = {0.5f, 0.5f};      // what a fall that begins now rolls from (Fall Speed, Timer Speed)
    int ticks = 0, writes = 0;
    float maxSpeed = 0.f, maxClock = 1.f;
    std::vector<float> writeVy;        // every velocity the module asked to be written
    double landedAt = -1.0;
    float landedFrom = 0.f;

    void Tick() {
        ++ticks;
        for (int i = 0; i < 3; ++i) last[i] = pos[i];
        pos[1] += vel[1];
        pos[0] += vel[0];
        maxSpeed = std::max(maxSpeed, -vel[1]);
        if (pos[1] <= 0.f) {
            if (!onGround && landedAt < 0.0) { landedAt = ms; landedFrom = vel[1]; }
            pos[1] = 0.f;
            onGround = true;
            vel[1] = -0.0784f;
        } else {
            onGround = false;
            vel[1] = (vel[1] - 0.08f) * 0.98f;
        }
        vel[0] *= 0.91f;
    }

    void Poll() {
        ms += 1.0;
        while (ms >= nextTick) {
            Tick();
            nextTick += 50.0 / clock;
        }
        Sample s;
        s.motionOk = true;
        std::memcpy(s.pos, pos, sizeof pos);
        std::memcpy(s.last, last, sizeof last);
        std::memcpy(s.vel, vel, sizeof vel);
        s.groundKnown = groundKnown; s.onGround = onGround;
        s.glideKnown = glideKnown; s.gliding = gliding;
        s.jumpHeld = jumpHeld;
        s.draw[0] = draw[0]; s.draw[1] = draw[1];
        const Result r = core.Step(p, s);
        clock = r.timer;
        maxClock = std::max(maxClock, clock);
        if (r.write) {
            ++writes;
            writeVy.push_back(r.vy);
            if (loseAll || (lose && (writes % 2) == 1)) { /* the game undoes this one */ }
            else vel[1] = r.vy;
        }
    }
    void RunUntilLanded(double limitMs = 20000.0) {
        const double start = ms;
        while (landedAt < 0.0 && ms - start < limitMs) Poll();
    }
};

void Test(const char* name, void (*fn)()) {
    const int before = g_failed;
    fn();
    std::printf("%-46s %s\n", name, g_failed == before ? "ok" : "FAILED");
}

// A fall of `height` blocks off a ledge: in the air at the top, standing speed.
Game Ledge(float height) {
    Game g;
    g.pos[1] = height;
    g.last[1] = height;
    g.onGround = false;
    g.core.Reset();
    return g;
}

// Cases.
void OffMeansVanilla() {
    Game g = Ledge(20.f);
    g.p.speedLo = g.p.speedHi = 0.f;
    g.p.timerLo = g.p.timerHi = 1.f;
    g.RunUntilLanded();
    CHECK(g.writes == 0, "writes %d", g.writes);
    CHECK(g.clock == 1.f, "clock %.2f", g.clock);
    CHECK(g.ticks >= 18 && g.ticks <= 28, "a vanilla 20 block fall took %d ticks", g.ticks);
}

void SpeedBringsTheFallDown() {
    Game vanilla = Ledge(20.f);
    vanilla.p.speedLo = vanilla.p.speedHi = 0.f;
    vanilla.RunUntilLanded();
    Game g = Ledge(20.f);
    g.p.speedLo = g.p.speedHi = 1.5f;
    g.RunUntilLanded();
    CHECK(g.writes >= 1, "writes %d", g.writes);
    CHECK(g.ticks * 10 <= vanilla.ticks * 6, "took %d ticks, vanilla %d", g.ticks, vanilla.ticks);
    CHECK(g.core.falls == 1, "falls %u", g.core.falls);
}

void TheFirstTickOffALedgeIsLeftAlone() {
    // -0.0784 is what standing still reads too: nothing is written until the fall is past its top, which is the
    // second tick's velocity (-0.155).
    Game g = Ledge(20.f);
    g.p.speedLo = g.p.speedHi = 2.f;
    for (int i = 0; i < 20; ++i) g.Poll();   // the first tick is at 25 ms
    CHECK(g.writes == 0 && g.ticks == 0, "writes %d ticks %d before the first tick", g.writes, g.ticks);
    while (g.ticks < 1) g.Poll();
    for (int i = 0; i < 3; ++i) g.Poll();
    CHECK(g.writes >= 1, "no write once the fall was past its top (vel %.4f)", g.vel[1]);
}

void NeverSlowsAFall() {
    Game g = Ledge(60.f);
    g.p.speedLo = g.p.speedHi = 1.f;
    g.vel[1] = -3.0f;
    g.last[1] = 60.f;
    for (int i = 0; i < 200 && g.landedAt < 0.0; ++i) g.Poll();
    CHECK(g.writes == 0, "writes %d (already faster than the floor)", g.writes);
}

void TimerRunsTheGameFaster() {
    Game vanilla = Ledge(20.f);
    vanilla.p.speedLo = vanilla.p.speedHi = 0.f;
    vanilla.RunUntilLanded();
    Game g = Ledge(20.f);
    g.p.speedLo = g.p.speedHi = 0.f;
    g.p.timerLo = g.p.timerHi = 2.5f;
    g.RunUntilLanded();
    CHECK(g.writes == 0, "writes %d", g.writes);
    CHECK(g.landedAt > 0 && g.landedAt < vanilla.landedAt / 2.0, "landed after %.0f ms, vanilla %.0f ms", g.landedAt, vanilla.landedAt);
    CHECK(g.clock == 1.f, "after landing the clock goes back: %.2f", g.clock);
}

void TimerIsCappedAndOnlyWhileFalling() {
    Game g = Ledge(30.f);
    g.p.speedLo = g.p.speedHi = 0.f;
    g.p.timerLo = g.p.timerHi = 9.f;
    bool sawFast = false;
    for (int i = 0; i < 4000 && g.landedAt < 0.0; ++i) { g.Poll(); if (g.clock > 4.9f) sawFast = true; CHECK(g.clock <= 5.f, "clock %.2f", g.clock); }
    CHECK(sawFast, "the clock never reached the cap");
    Game h;   // standing on the ground: the clock is left alone
    h.p.timerLo = h.p.timerHi = 3.f;
    for (int i = 0; i < 500; ++i) h.Poll();
    CHECK(h.clock == 1.f, "clock %.2f while standing", h.clock);
}

void AJumpsFallOnlyWithAfterJumps() {
    for (int with = 0; with < 2; ++with) {
        Game g;                      // standing
        g.p.speedLo = g.p.speedHi = 2.f;
        g.p.afterJump = with == 1;
        g.jumpHeld = true;
        for (int i = 0; i < 30; ++i) g.Poll();
        g.onGround = false;          // the jump: off the ground at 0.42
        g.vel[1] = 0.42f;
        g.pos[1] = 0.1f;
        g.last[1] = 0.f;
        g.RunUntilLanded();
        if (with) CHECK(g.writes >= 1, "After Jumps: writes %d", g.writes);
        else CHECK(g.writes == 0, "a jump's own fall: writes %d", g.writes);
    }
}

void AFallWithoutAJumpKeyIsAFallEvenGoingUpAtFirst() {
    // knocked up (no jump key held): the fall that follows counts
    Game g;
    g.p.speedLo = g.p.speedHi = 2.f;
    g.jumpHeld = false;
    for (int i = 0; i < 30; ++i) g.Poll();
    g.onGround = false;
    g.vel[1] = 0.5f;
    g.pos[1] = 0.1f;
    g.RunUntilLanded();
    CHECK(g.writes >= 1, "writes %d", g.writes);
}

void GlidingIsLeftAlone() {
    Game g = Ledge(40.f);
    g.p.speedLo = g.p.speedHi = 2.f;
    g.p.timerLo = g.p.timerHi = 2.f;
    g.gliding = true;
    for (int i = 0; i < 3000 && g.landedAt < 0.0; ++i) g.Poll();
    CHECK(g.writes == 0 && g.clock == 1.f, "writes %d clock %.2f", g.writes, g.clock);
}

void WhatIsNotKnownIsNotDone() {
    Game a = Ledge(30.f);
    a.p.speedLo = a.p.speedHi = 2.f;
    a.groundKnown = false;
    for (int i = 0; i < 1500 && a.landedAt < 0.0; ++i) a.Poll();
    CHECK(a.writes == 0, "ground flag unknown: writes %d", a.writes);
    Game b = Ledge(30.f);
    b.p.speedLo = b.p.speedHi = 2.f;
    b.glideKnown = false;
    for (int i = 0; i < 1500 && b.landedAt < 0.0; ++i) b.Poll();
    CHECK(b.writes == 0, "gliding flag unknown: writes %d", b.writes);
}

void TheSpeedIsCappedAtTheGamesTerminal() {
    Game g = Ledge(200.f);
    g.p.speedLo = g.p.speedHi = 9.f;
    g.RunUntilLanded();
    CHECK(g.maxSpeed <= 3.921f, "fell at %.3f a tick", g.maxSpeed);
    CHECK(g.maxSpeed > 3.5f, "never got close to the cap: %.3f", g.maxSpeed);
}

void ALostWriteIsMadeAgain() {
    Game g = Ledge(20.f);
    g.p.speedLo = g.p.speedHi = 1.5f;
    g.lose = true;   // the game undoes every other write
    g.RunUntilLanded();
    CHECK(g.writes >= 2, "writes %d", g.writes);
    Game ok = Ledge(20.f);
    ok.p.speedLo = ok.p.speedHi = 1.5f;
    ok.RunUntilLanded();
    CHECK(g.ticks <= ok.ticks + 2, "with lost writes %d ticks, without %d", g.ticks, ok.ticks);
}

void AGameThatUndoesEveryWriteIsNotHammered() {
    Game g = Ledge(20.f);
    g.p.speedLo = g.p.speedHi = 1.5f;
    g.loseAll = true;
    g.RunUntilLanded();
    CHECK(g.writes > 0, "writes %d", g.writes);
    CHECK(g.writes <= g.ticks * kWritesPerTick, "%d writes over %d ticks", g.writes, g.ticks);
}

void EachFallIsCountedOnce() {
    Game g = Ledge(10.f);
    g.p.speedLo = g.p.speedHi = 1.f;
    g.RunUntilLanded();
    const unsigned first = g.core.falls;
    g.landedAt = -1.0;
    g.pos[1] = 10.f; g.last[1] = 10.f; g.onGround = false; g.vel[1] = -0.0784f;   // off the next ledge
    g.RunUntilLanded();
    CHECK(first == 1 && g.core.falls == 2, "falls %u then %u", first, g.core.falls);
}

void StandingDoesNothing() {
    Game g;
    g.p.speedLo = g.p.speedHi = 3.f;
    g.p.timerLo = g.p.timerHi = 3.f;
    for (int i = 0; i < 1000; ++i) g.Poll();
    CHECK(g.writes == 0 && g.clock == 1.f && g.core.falls == 0, "writes %d clock %.2f falls %u", g.writes, g.clock, g.core.falls);
}

// Ranges: each fall rolls its own Fall Speed and Timer Speed.
void ARollIsAThousandthOfTheWayAlongTheRange() {
    const float top = 0.99999994f;   // the largest draw: just under 1
    CHECK(Roll(1.4f, 1.6f, 0.f) == 1.4f, "low end %.4f", Roll(1.4f, 1.6f, 0.f));
    CHECK(Roll(1.4f, 1.6f, top) == 1.6f, "high end %.4f", Roll(1.4f, 1.6f, top));
    CHECK(Roll(1.4f, 1.6f, 0.5f) == 1.5f, "middle %.4f", Roll(1.4f, 1.6f, 0.5f));
    CHECK(Roll(2.f, 2.f, 0.f) == 2.f && Roll(2.f, 2.f, 0.7f) == 2.f && Roll(2.f, 2.f, top) == 2.f, "a range of one number is that number");
    CHECK(Roll(1.6f, 1.4f, 0.f) == 1.4f && Roll(1.6f, 1.4f, top) == 1.6f, "ends the wrong way round are put right");
    CHECK(Roll(0.f, kTerminal, top) == kTerminal, "the game's terminal velocity can be reached: %.4f", Roll(0.f, kTerminal, top));
    CHECK(Roll(NAN, 1.f, 0.5f) == 0.f && Roll(1.f, INFINITY, 0.5f) == 0.f && Roll(1.f, 2.f, NAN) == 0.f, "a number that is not one rolls 0, which does nothing");
    CHECK(Roll(1.f, 2.f, -3.f) == 1.f && Roll(1.f, 2.f, 7.f) == 2.f, "a draw outside 0 to 1 is held to the ends");
}

void RollsSpreadEvenlyOverEveryThousandth() {
    // 100,000 draws swept evenly over 0 to 1 into 1.400 to 1.600, which is 201 numbers: each comes up 497 or 498
    // times, the two ends as often as the rest.
    int hits[201] = {};
    bool inside = true, onGrid = true;
    for (int i = 0; i < 100000; ++i) {
        const float v = Roll(1.4f, 1.6f, (float)i / 100000.f);
        const long k = std::lround(v * 1000.f) - 1400;
        onGrid = onGrid && std::fabs(v * 1000.f - std::round(v * 1000.f)) < 0.01f;
        if (k < 0 || k > 200) inside = false;
        else ++hits[k];
    }
    int lowest = 1 << 30, highest = 0;
    for (int h : hits) { lowest = std::min(lowest, h); highest = std::max(highest, h); }
    CHECK(inside && onGrid, "inside %d on the thousandths %d", inside, onGrid);
    CHECK(lowest >= 497 && highest <= 498, "a number came up from %d to %d times", lowest, highest);
    CHECK(hits[0] >= 497 && hits[200] >= 497, "the ends came up %d and %d times", hits[0], hits[200]);
}

void AFallRollsOnceAndKeepsItForTheWholeFall() {
    Game g = Ledge(30.f);
    g.p.speedLo = 1.f; g.p.speedHi = 2.f;
    g.p.timerLo = g.p.timerHi = 1.f;
    g.lose = true;           // the game undoes every other write, so the module writes more than once in a fall
    g.draw[0] = 0.25f;       // 1.000 and a quarter of the way: 1.250
    for (int i = 0; i < 4000 && g.landedAt < 0.0; ++i) {
        g.Poll();
        if (g.core.Acting()) g.draw[0] = (i % 2) ? 0.95f : 0.05f;   // once it has begun, the draws are worth nothing to it
    }
    CHECK(g.writeVy.size() >= 2, "only %zu writes", g.writeVy.size());
    bool same = !g.writeVy.empty();
    for (float v : g.writeVy) same = same && v == g.writeVy.front();
    CHECK(same, "the fall's writes were not all one speed");
    CHECK(!g.writeVy.empty() && g.writeVy.front() == -1.25f, "wrote %.4f, wanted -1.2500", g.writeVy.empty() ? 0.f : g.writeVy.front());

    // the next fall rolls again
    g.writeVy.clear();
    g.landedAt = -1.0;
    g.pos[1] = 30.f; g.last[1] = 30.f; g.onGround = false; g.vel[1] = -0.0784f;
    g.draw[0] = 0.75f;       // 1.750
    for (int i = 0; i < 4000 && g.landedAt < 0.0; ++i) {
        g.Poll();
        if (g.core.Acting()) g.draw[0] = 0.5f;
    }
    CHECK(!g.writeVy.empty() && g.writeVy.front() == -1.75f, "the second fall wrote %.4f, wanted -1.7500", g.writeVy.empty() ? 0.f : g.writeVy.front());
    CHECK(g.core.falls == 2, "falls %u", g.core.falls);
}

void TheTimerRollsToo() {
    Game g = Ledge(30.f);
    g.p.speedLo = g.p.speedHi = 0.f;
    g.p.timerLo = 2.f; g.p.timerHi = 4.f;
    g.draw[1] = 0.5f;        // 2.000 and half of the way: 3.000
    g.RunUntilLanded();
    CHECK(g.maxClock == 3.f, "the clock ran at x%.4f, wanted x3.0000", g.maxClock);
    CHECK(g.clock == 1.f, "after landing the clock goes back: %.2f", g.clock);
    g.maxClock = 1.f;
    g.landedAt = -1.0;
    g.pos[1] = 30.f; g.last[1] = 30.f; g.onGround = false; g.vel[1] = -0.0784f;
    g.draw[1] = 0.f;         // 2.000
    g.RunUntilLanded();
    CHECK(g.maxClock == 2.f, "the second fall's clock ran at x%.4f, wanted x2.0000", g.maxClock);
}

void ARangeThatReachesTheBottomSometimesLeavesTheFallAlone() {
    // Fall Speed 0 to 2: the lowest draw rolls 0, which leaves that fall's speed as it is; a high one boosts.
    Game a = Ledge(20.f);
    a.p.speedLo = 0.f; a.p.speedHi = 2.f;
    a.draw[0] = 0.f;
    a.RunUntilLanded();
    CHECK(a.writes == 0, "writes %d on a fall that rolled 0", a.writes);
    Game b = Ledge(20.f);
    b.p.speedLo = 0.f; b.p.speedHi = 2.f;
    b.draw[0] = 0.9f;
    b.RunUntilLanded();
    CHECK(!b.writeVy.empty() && b.writeVy.front() == -1.8f, "wrote %.4f, wanted -1.8000", b.writeVy.empty() ? 0.f : b.writeVy.front());
    // and Timer Speed 1 to 3: the lowest draw is 1, the clock left alone
    Game c = Ledge(20.f);
    c.p.speedLo = c.p.speedHi = 0.f;
    c.p.timerLo = 1.f; c.p.timerHi = 3.f;
    c.draw[1] = 0.f;
    c.RunUntilLanded();
    CHECK(c.maxClock == 1.f, "the clock ran at x%.4f on a fall that rolled 1", c.maxClock);
    Game d = Ledge(20.f);
    d.p.speedLo = d.p.speedHi = 0.f;
    d.p.timerLo = 1.f; d.p.timerHi = 3.f;
    d.draw[1] = 0.9f;
    d.RunUntilLanded();
    CHECK(d.maxClock == 2.8f, "the clock ran at x%.4f, wanted x2.8000", d.maxClock);
}

void EqualEndsAreTheSameEveryTime() {
    for (float draw : {0.f, 0.3f, 0.99999994f}) {
        Game g = Ledge(20.f);
        g.p.speedLo = g.p.speedHi = 1.7f;
        g.p.timerLo = g.p.timerHi = 2.5f;
        g.draw[0] = g.draw[1] = draw;
        g.RunUntilLanded();
        CHECK(!g.writeVy.empty() && g.writeVy.front() == -1.7f, "draw %.2f: wrote %.4f", draw, g.writeVy.empty() ? 0.f : g.writeVy.front());
        CHECK(g.maxClock == 2.5f, "draw %.2f: the clock ran at x%.4f", draw, g.maxClock);
    }
}

}  // namespace

int main() {
    Test("off means the vanilla fall", OffMeansVanilla);
    Test("speed brings the fall down", SpeedBringsTheFallDown);
    Test("the first tick off a ledge is left alone", TheFirstTickOffALedgeIsLeftAlone);
    Test("it never slows a fall", NeverSlowsAFall);
    Test("the timer runs the game faster", TimerRunsTheGameFaster);
    Test("the timer is capped, and only while falling", TimerIsCappedAndOnlyWhileFalling);
    Test("a jump's fall only with After Jumps", AJumpsFallOnlyWithAfterJumps);
    Test("a knock up and the fall after it count", AFallWithoutAJumpKeyIsAFallEvenGoingUpAtFirst);
    Test("gliding is left alone", GlidingIsLeftAlone);
    Test("what is not known is not done", WhatIsNotKnownIsNotDone);
    Test("the speed is capped at the game's terminal", TheSpeedIsCappedAtTheGamesTerminal);
    Test("a write the game undoes is made again", ALostWriteIsMadeAgain);
    Test("a game that undoes every write is not hammered", AGameThatUndoesEveryWriteIsNotHammered);
    Test("each fall is counted once", EachFallIsCountedOnce);
    Test("standing does nothing", StandingDoesNothing);
    Test("a roll is a thousandth of the way along", ARollIsAThousandthOfTheWayAlongTheRange);
    Test("rolls spread evenly, the ends included", RollsSpreadEvenlyOverEveryThousandth);
    Test("a fall rolls once and keeps it", AFallRollsOnceAndKeepsItForTheWholeFall);
    Test("the timer rolls too", TheTimerRollsToo);
    Test("a range down to 0 or 1 can leave a fall alone", ARangeThatReachesTheBottomSometimesLeavesTheFallAlone);
    Test("equal ends are the same every time", EqualEndsAreTheSameEveryTime);
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
