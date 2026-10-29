#include "../../backend/backend_impl.h"
#include "citron/option_record.h"
#include <algorithm>
#include <cmath>

namespace citron {

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
    zoomActive = false;
    customFovOn = false;
}

// While zooming, the wheel's step of the selected slot adds nothing (the add becomes a mov), so a notch
// sizes the zoom and the hotbar stays where it is, for the game and the server alike.
void Backend::Impl::SetWheelSuppressed(bool on) {
    const uintptr_t at = SiteAt(game::Site::HotbarWheel);
    if (!at) return;
    static const uint8_t kAdd = 0x03, kMov = 0x8B;
    if (on && !hotbarWheel.on) WritePatch(hotbarWheel, at + 7, &kMov, 1, &kAdd);
    else if (!on && hotbarWheel.on) ClearPatch(hotbarWheel);
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
    if (!s || !s->on || !fovAddr || !InGame()) {
        if (zoomActive || hotbarWheel.on) RestoreZoom();
        return;
    }
    const bool held = s->bind > 0 && PhysKeyDown(s->bind);
    const float configured = std::clamp(Num(*s, "fov", 30.f), 5.f, 110.f);
    const bool smooth = Check(*s, "smooth", true);
    const bool wheel = Check(*s, "scroll", true);
    const float stepDeg = Num(*s, "step", 10.f) * (Check(*s, "invertScroll", false) ? -1.f : 1.f);
    const auto now = clock_t_::now();

    if (held && !zoomActive) {
        if (!FovRecordLooksRight(fovAddr, &zoomPrevFov)) return;
        zoomCurrentFov = zoomPrevFov;
        zoomScrollAdjust = 0.f;
        zoomActive = true;
        zoomLastTick = now;
        TakeWheelNotches();
    }
    if (!zoomActive) return;
    SetWheelOwned(held && wheel, proc.pid);
    SetWheelSuppressed(held && wheel);

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
