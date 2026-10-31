#include "../../backend/backend_impl.h"
#include "citron/option_record.h"
#include <algorithm>
#include <cmath>

namespace citron {
namespace {

// selectSlot opens with push rbp, r15, r14, r13: the bytes the guard takes the place of.
constexpr int kSelectSlotStolen = 7;

}  // namespace

bool Backend::Impl::FovRecordLooksRight(uintptr_t valueAddr, float* valueOut) const {
    if (!proc.ok() || valueAddr < optrec::kValue) return false;
    const uintptr_t record = valueAddr - optrec::kValue;
    uint8_t raw[optrec::kBytes] = {};
    if (!engine::Read(proc, record, raw, sizeof raw)) return false;
    return optrec::LooksLikeFovOption(raw, record, (uint64_t)(uintptr_t)proc.base, (uint64_t)proc.size, valueOut);
}

// The FOV option's value lives in an allocated record, not the image: a slice of memory per pass
// is searched for its default (60) and step, and a match counts once it is shaped like the record.
void Backend::Impl::ScanFov() {
    if (fovAddr || fovScanDone) return;
    static const std::vector<int> pat = {-1, -1, -1, -1, 0x00, 0x00, 0x70, 0x42, 0x6F, 0x12};
    std::vector<uint8_t> scratch;
    engine::DataSlice slice{};
    for (int budget = 0; budget < 2; ++budget) {
        if (!engine::NextDataSlice(proc, fovScanCursor, 1u << 20, pat.size(), scratch, slice)) {
            fovScanDone = true;
            Log("[fov] the FOV option was not found");
            return;
        }
        if (!slice.data || slice.size < pat.size()) continue;
        for (size_t i = 0; i + pat.size() <= slice.size; ++i) {
            bool hit = true;
            for (size_t k = 0; k < pat.size() && hit; ++k) hit = pat[k] < 0 || slice.data[i + k] == (uint8_t)pat[k];
            if (!hit || !FovRecordLooksRight(slice.address + i)) continue;
            fovAddr = slice.address + i;
            Log("[fov] the FOV option is at 0x%llX", (unsigned long long)fovAddr);
            return;
        }
    }
}

void Backend::Impl::FovTick(const Modules& m) {
    if (!On(m, "zoom") && !On(m, "customfov")) return;
    if (!fovAddr) {
        ScanFov();
        return;
    }
    const auto now = clock_t_::now();
    if (now < fovNextLook) return;
    fovNextLook = now + std::chrono::milliseconds(500);
    if (FovRecordLooksRight(fovAddr)) {
        fovStaleLooks = 0;
        return;
    }
    // Twice, because one failed read is not evidence: then the record is gone and the search begins again.
    if (++fovStaleLooks < 2) return;
    Log("[fov] the FOV option moved; searching again");
    fovStaleLooks = 0;
    fovAddr = 0;
    fovScanCursor = 0;
    fovScanDone = false;
    fovBoundsRead = false;
    zoomActive = false;
    customFovOn = false;
}

// The game's getFov clamps the option to its record's own bounds (30 to 110), multiplies it by the
// sprint's modifier and clamps that to 5 to 130. The bounds are data, so they move with what is asked
// of them; the 130 is code, and its load reads a float of ours while Custom FOV needs more. It stays
// under 180: past that the tangent of the half angle turns over and the world draws upside down.
constexpr float kFovCeiling = 175.f, kSprintWidens = 1.1f, kGameCeiling = 130.f;

void Backend::Impl::SetFovBounds(float lo, float hi) {
    const uintptr_t record = fovAddr - optrec::kValue;
    if (lo != fovMinSet && engine::Write(proc, record + optrec::kMin, lo)) fovMinSet = lo;
    if (hi != fovMaxSet && engine::Write(proc, record + optrec::kMax, hi)) fovMaxSet = hi;
}

void Backend::Impl::RestoreFovBounds() {
    if (fovAddr && fovBoundsRead) SetFovBounds(fovMinGiven, fovMaxGiven);
}

void Backend::Impl::SetFovCeiling(bool lifted) {
    const uintptr_t at = SiteAt(game::Site::FovLimit);
    if (!at || lifted == fovLimit.on) return;
    if (!lifted) {
        ClearPatch(fovLimit);
        return;
    }
    if (!fovCeiling) {
        fovCeiling = (uintptr_t)engine::AllocCaveNear(proc, at, 0x1000);
        if (!fovCeiling || !engine::Write(proc, fovCeiling, kFovCeiling)) return;
    }
    const int32_t disp = (int32_t)((int64_t)fovCeiling - (int64_t)(at + 16));
    WritePatch(fovLimit, at + 12, reinterpret_cast<const uint8_t*>(&disp), 4, nullptr);
}

void Backend::Impl::FovBoundsTick(const Modules& m) {
    if (!fovAddr) return;
    if (!fovBoundsRead) {
        const uintptr_t record = fovAddr - optrec::kValue;
        if (!engine::Read(proc, record + optrec::kMin, fovMinGiven) || !engine::Read(proc, record + optrec::kMax, fovMaxGiven)) return;
        fovMinSet = fovMinGiven;
        fovMaxSet = fovMaxGiven;
        fovBoundsRead = true;
    }
    const ModuleSnapshot* s = Mod(m, "customfov");
    const float custom = customFovOn && s ? std::clamp(Num(*s, "fov", 120.f), 10.f, 170.f) : 0.f;
    float lo = fovMinGiven, hi = fovMaxGiven;
    if (custom > 0.f) {
        lo = std::min(lo, custom);
        hi = std::max(hi, custom);
    }
    if (zoomActive) lo = std::min(lo, 5.f);
    SetFovBounds(lo, hi);
    SetFovCeiling(custom * kSprintWidens > kGameCeiling);
}

// While zooming the wheel sizes the zoom and the hotbar stays where it is, for the game and the server
// alike. The game turns the hotbar from raw input, by more than one way, so the guard stands where they
// all end, selectSlot, and refuses a step of one slot while the byte is set.
void Backend::Impl::SetWheelSuppressed(bool on) {
    if (on == slotsHeld || !slotHook.installed) return;
    const uint8_t flag = on ? 1 : 0;
    if (engine::Write(proc, slotHook.slot, flag)) slotsHeld = on;
}

void Backend::Impl::RestoreZoom() {
    if (zoomActive && fovAddr && zoomPrevFov > 1.f && zoomPrevFov < 179.f) engine::Write(proc, fovAddr, zoomPrevFov);
    zoomActive = false;
    zoomScrollAdjust = 0.f;
    SetWheelOwned(false, 0);
    SetWheelSuppressed(false);
}

void Backend::Impl::ZoomTick(const Modules& m) {
    const ModuleSnapshot* s = Mod(m, "zoom");
    const auto now = clock_t_::now();
    const bool guard = s && s->on && Check(*s, "scroll", true);
    if (guard && !slotHook.installed && now >= nextSlotTry) {
        const uintptr_t at = SiteAt(game::Site::SelectSlot);
        if (!at || !engine::InstallSlotGuardHook(proc, slotHook, at, kSelectSlotStolen)) nextSlotTry = now + std::chrono::seconds(5);
    } else if (!guard && slotHook.installed) {
        SetWheelSuppressed(false);
        engine::RemoveHook(proc, slotHook);
        slotHook = engine::CaveHook{};
        slotsHeld = false;
    }
    if (!s || !s->on || !fovAddr || !InGame()) {
        if (zoomActive || slotsHeld) RestoreZoom();
        return;
    }
    const bool held = s->bind > 0 && PhysKeyDown(s->bind);
    const float configured = std::clamp(Num(*s, "fov", 30.f), 5.f, 110.f);
    const bool smooth = Check(*s, "smooth", true);
    const bool wheel = Check(*s, "scroll", true);
    const float stepDeg = Num(*s, "step", 10.f) * (Check(*s, "invertScroll", false) ? -1.f : 1.f);

    if (held && !zoomActive) {
        if (!FovRecordLooksRight(fovAddr, &zoomPrevFov)) return;
        zoomCurrentFov = zoomPrevFov;
        zoomScrollAdjust = 0.f;
        zoomActive = true;
        zoomLastTick = now;
        TakeWheelNotches();
    }
    if (!zoomActive) return;
    // A number key still picks its slot: the guard stands aside for a moment after one.
    for (int i = 0; i < 9; ++i) {
        const uint32_t presses = PhysKeyEdges(binds.hotbar[i]);
        if (presses != hotbarSeen[i]) slotsFreeAt = now + std::chrono::milliseconds(150);
        hotbarSeen[i] = presses;
    }
    SetWheelOwned(held && wheel, proc.pid);
    SetWheelSuppressed(held && wheel && now >= slotsFreeAt);

    const float dt = std::min(std::chrono::duration<float>(now - zoomLastTick).count(), 0.1f);
    zoomLastTick = now;
    float target = zoomPrevFov;
    if (held) {
        if (const int notches = TakeWheelNotches(); wheel && notches)
            zoomScrollAdjust = std::clamp(zoomScrollAdjust + notches * stepDeg, 5.f - configured, 110.f - configured);
        target = std::clamp(configured + zoomScrollAdjust, 5.f, 110.f);
    }
    if (smooth) {
        const float diff = target - zoomCurrentFov;
        zoomCurrentFov = std::fabs(diff) < 0.5f ? target : zoomCurrentFov + diff * std::clamp(dt * 20.f, 0.05f, 1.f);
    } else {
        zoomCurrentFov = target;
    }
    if (!held && std::fabs(zoomCurrentFov - zoomPrevFov) < 0.5f) {
        RestoreZoom();
        return;
    }
    engine::Write(proc, fovAddr, zoomCurrentFov);
}

// Your field of view held at a number of your own, past the 30 to 110 the game's slider allows.
void Backend::Impl::CustomFovTick(const Modules& m) {
    const ModuleSnapshot* s = Mod(m, "customfov");
    const bool want = s && s->on && fovAddr;
    if (!want) {
        if (customFovOn && fovAddr && !zoomActive && customFovOriginal > 1.f) engine::Write(proc, fovAddr, customFovOriginal);
        customFovOn = false;
        return;
    }
    if (zoomActive) return;
    const float fov = std::clamp(Num(*s, "fov", 120.f), 10.f, 170.f);
    if (!customFovOn) {
        float now = 0.f;
        if (!FovRecordLooksRight(fovAddr, &now)) return;
        customFovOriginal = now;
        customFovOn = true;
    }
    float live = 0.f;
    if (engine::Read(proc, fovAddr, live) && std::fabs(live - fov) > 0.01f) engine::Write(proc, fovAddr, fov);
}

}  // namespace citron
