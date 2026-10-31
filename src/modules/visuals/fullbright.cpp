#include "../../backend/backend_impl.h"
#include "citron/option_record.h"
#include <cmath>
#include <cstring>

namespace citron {
namespace {

constexpr float kGamma = 100.f;
constexpr int kRecordBytes = 0x28;

}  // namespace

bool Backend::Impl::GammaRecordLooksRight(uintptr_t record, float& value, float& fallback) const {
    uint8_t raw[kRecordBytes] = {};
    if (!engine::Read(proc, record, raw, sizeof raw)) return false;
    uintptr_t vtable = 0;
    std::memcpy(&vtable, raw + optrec::kVTable, sizeof vtable);
    const uintptr_t base = (uintptr_t)proc.base;
    if (!base || vtable < base || vtable >= base + proc.size) return false;
    float mn, mx, val, dflt, step;
    std::memcpy(&mn, raw + optrec::kMin, 4);
    std::memcpy(&mx, raw + optrec::kMax, 4);
    std::memcpy(&val, raw + optrec::kValue, 4);
    std::memcpy(&dflt, raw + optrec::kDefault, 4);
    std::memcpy(&step, raw + optrec::kStep, 4);
    for (float f : {mn, mx, val, dflt, step})
        if (!std::isfinite(f)) return false;
    if (!(mx > mn) || mn < -1.0e6f || mx > 1.0e6f || mx - mn < 1.0e-3f) return false;
    if (step < 1.0e-5f || step > mx - mn || dflt < mn || dflt > mx) return false;
    value = val;
    fallback = val >= mn && val <= mx ? val : dflt;
    return true;
}

void Backend::Impl::RestoreGamma() {
    if (gammaPinned && proc.ok()) engine::Write(proc, gammaRecord + optrec::kValue, gammaOriginal);
    gammaPinned = false;
}

// The renderer asks Options::getGamma for the brightness every frame and that reads this record, so a
// number written here is on screen at once.
void Backend::Impl::FullbrightTick(const Modules& m) {
    if (!On(m, "fullbright")) {
        RestoreGamma();
        return;
    }
    const auto now = clock_t_::now();
    if (now < gammaNextEnforce) return;
    gammaNextEnforce = now + std::chrono::milliseconds(100);
    float value = 0, fallback = 0;
    if (!gammaRecord || !GammaRecordLooksRight(gammaRecord, value, fallback)) {
        gammaPinned = false;
        if (!rd.OptionRecord("gfx_gamma", gammaRecord) || !GammaRecordLooksRight(gammaRecord, value, fallback)) {
            gammaRecord = 0;
            gammaNextEnforce = now + std::chrono::seconds(1);
            return;
        }
        Log("[fullbright] the brightness option is at 0x%llX", (unsigned long long)gammaRecord);
    }
    if (!gammaPinned) {
        gammaOriginal = fallback;
        gammaPinned = true;
    }
    if (std::fabs(value - kGamma) > 0.01f) engine::Write(proc, gammaRecord + optrec::kValue, kGamma);
}

}  // namespace citron
