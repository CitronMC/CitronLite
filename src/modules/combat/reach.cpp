#include "../../backend/backend_impl.h"
#include <algorithm>
#include <cstring>

namespace citron {
namespace {

// What the game itself has at the bytes Reach and Ghost Hand change.
constexpr uint8_t kJe52[2] = {0x74, 0x0D}, kMinss52 = 0x5D, kMovss = 0x10, kJbe60[2] = {0x76, 0x09};
constexpr uint8_t kNops[2] = {0x90, 0x90};

}  // namespace

// The pick chooses how far a hit reaches, then holds the distance to the block under the crosshair
// to it. The game's own 3.0 and 7.0 are pooled constants many other places read, so they are never
// written: the loads are pointed at floats of our own on a page within reach of the code.
//
// 1.26.52: `je over; minss xmm7, [7.0]` (creative: the nearer of the wall and 7), then survival's
// clamp to 3. The je goes, creative's minss reads our float, and Ghost Hand turns it into a movss so
// the wall no longer counts.
// 1.26.60: `test al, al; jne creative; movss xmm7, [3.0]; jmp; creative: movss xmm7, [7.0]`, then
// survival's wall test (the jbe after ReachWall). Both loads read our floats; Ghost Hand drops the jbe.
//
// A load that already points outside the game was left that way by a run that never got to put it
// back. Where it pointed first is gone, so ours takes its place and keeps it, holding the game's own
// numbers whenever Reach is off.
bool Backend::Impl::ReachInstall() {
    if (reachInstalled) return true;
    const uintptr_t at = SiteAt(game::Site::Reach);
    if (!at) return false;
    if (!reachFloats) {
        reachFloats = (uintptr_t)engine::AllocCaveNear(proc, at, 0x1000);
        if (!reachFloats) return false;
    }
    if (!engine::Read(proc, at, reachOriginal, sizeof reachOriginal)) return false;
    const uintptr_t base = (uintptr_t)proc.base, top = base + proc.size;
    auto outside = [&](size_t operand) {
        int32_t d = 0;
        std::memcpy(&d, reachOriginal + operand, 4);
        const uintptr_t to = at + operand + 4 + (intptr_t)d;
        return to < base || to >= top;
    };
    const float survival = 3.f, creative = 7.f;
    if (build == game::Build::V1_26_52) {
        std::memcpy(reachOriginal, kJe52, 2);
        reachOriginal[4] = kMinss52;
        reachKeep = outside(6);
        engine::Write(proc, reachFloats, creative);
        const int32_t disp = (int32_t)((int64_t)reachFloats - (int64_t)(at + 10));
        if (!engine::WriteProtected(proc, at + 6, &disp, 4)) return false;
    } else {
        reachKeep = outside(8) || outside(18);
        engine::Write(proc, reachFloats, survival);
        engine::Write(proc, reachFloats + 4, creative);
        const int32_t s = (int32_t)((int64_t)reachFloats - (int64_t)(at + 12));
        const int32_t c = (int32_t)((int64_t)(reachFloats + 4) - (int64_t)(at + 22));
        if (!engine::WriteProtected(proc, at + 8, &s, 4)) return false;
        if (!engine::WriteProtected(proc, at + 18, &c, 4)) {
            if (!reachKeep) engine::WriteProtected(proc, at + 8, reachOriginal + 8, 4);
            return false;
        }
    }
    reachInstalled = true;
    Log("[reach] the pick reads its reach at 0x%llX%s", (unsigned long long)reachFloats,
        reachKeep ? " (taking over a load an earlier run left pointed away)" : "");
    return true;
}

void Backend::Impl::ReachRestore() {
    const uintptr_t at = SiteAt(game::Site::Reach);
    if (!reachInstalled || !at || !proc.ok()) {
        reachInstalled = false;
        reachState = 0;
        return;
    }
    if (build == game::Build::V1_26_52) {
        engine::Write(proc, reachFloats, 7.f);
        engine::WriteProtected(proc, at, kJe52, 2);
        engine::WriteProtected(proc, at + 4, &kMinss52, 1);
        if (!reachKeep) engine::WriteProtected(proc, at + 6, reachOriginal + 6, 4);
    } else {
        engine::Write(proc, reachFloats, 3.f);
        engine::Write(proc, reachFloats + 4, 7.f);
        if (!reachKeep) {
            engine::WriteProtected(proc, at + 8, reachOriginal + 8, 4);
            engine::WriteProtected(proc, at + 18, reachOriginal + 18, 4);
        }
        if (const uintptr_t wall = SiteAt(game::Site::ReachWall)) engine::WriteProtected(proc, wall + 0xB, kJbe60, 2);
    }
    // The page stays: a thread may still be inside an instruction that read the old operand.
    reachInstalled = false;
    reachState = 0;
}

void Backend::Impl::ReachTick(const Modules& m) {
    const ModuleSnapshot* r = Mod(m, "reach");
    bool reachOn = r && r->on;
    const bool ghostOn = On(m, "ghosthand");
    float value = 3.f;
    if (reachOn) {
        float lo = Num(*r, "distance_min", 3.f), hi = Num(*r, "distance_max", 3.f);
        if (hi < lo) std::swap(lo, hi);
        const auto now = clock_t_::now();
        if (now >= reachNextRoll) {
            reachTarget = hi > lo ? lo + Rand01() * (hi - lo) : lo;
            reachNextRoll = now + std::chrono::milliseconds(220 + (int)(Rand01() * 260));
            reachChanceOn = ChanceAllows(*r);
        }
        reachOn = reachChanceOn;
        value = std::clamp(reachTarget, 3.f, 7.f);
    }
    const uintptr_t at = SiteAt(game::Site::Reach);
    if (!at) {
        curReach = 3.f;
        return;
    }
    const int want = ghostOn ? 2 : reachOn ? 1 : 0;
    if (!want) {
        if (reachInstalled && reachState) {
            if (build == game::Build::V1_26_52) {
                engine::WriteProtected(proc, at, kJe52, 2);
                engine::WriteProtected(proc, at + 4, &kMinss52, 1);
                engine::Write(proc, reachFloats, 7.f);
            } else {
                engine::Write(proc, reachFloats, 3.f);
                if (const uintptr_t wall = SiteAt(game::Site::ReachWall))
                    engine::WriteProtected(proc, wall + 0xB, kJbe60, 2);
            }
            reachState = 0;
        }
        curReach = 3.f;
        return;
    }
    if (!ReachInstall()) return;
    const float distance = reachOn ? value : 3.f;
    engine::Write(proc, reachFloats, distance);
    if (want != reachState) {
        if (build == game::Build::V1_26_52) {
            const uint8_t op = want == 2 ? kMovss : kMinss52;
            engine::WriteProtected(proc, at, kNops, 2);
            engine::WriteProtected(proc, at + 4, &op, 1);
        } else if (const uintptr_t wall = SiteAt(game::Site::ReachWall)) {
            engine::WriteProtected(proc, wall + 0xB, want == 2 ? kNops : kJbe60, 2);
        }
        reachState = want;
    }
    curReach = distance;
}

}  // namespace citron
