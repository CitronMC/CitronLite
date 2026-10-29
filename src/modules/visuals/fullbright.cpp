#include "../../backend/backend_impl.h"
#include "citron/option_record.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace citron {
namespace {

constexpr float kGamma = 100.f;
// The gamma option's default (0.5) and step (0.001), which follow the value in its record.
constexpr uint8_t kGammaTail[8] = {0x00, 0x00, 0x00, 0x3F, 0x6F, 0x12, 0x83, 0x3A};
constexpr int kRecordBytes = 0x28;
constexpr uintptr_t kDescriptor = 0x08, kDescriptorSpan = 0x400;

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

// The brightness option is found three ways in turn, a slice of memory per pass: its key
// ("gfx_gamma"), the records whose descriptor points near that key, and every record with the
// option's own default and step. The renderer does not read only the copy the key leads to, so
// every copy is held.
void Backend::Impl::GammaScanStep() {
    if (gammaScan.pass < 1 || gammaScan.pass > 3) return;
    constexpr size_t kSlice = 512u << 10, kBudget = 2u << 20;
    static const char kKey[] = "gfx_gamma";
    constexpr size_t kKeyLen = sizeof(kKey) - 1;
    size_t spent = 0;
    engine::DataSlice slice;
    while (spent < kBudget &&
           engine::NextDataSlice(proc, gammaScan.cursor, kSlice, gammaScan.pass == 1 ? kKeyLen : 8u, gammaScan.scratch, slice)) {
        spent += slice.size;
        const size_t reach = slice.size + slice.overlap;
        if (gammaScan.pass == 1) {
            for (size_t at = 0; at + kKeyLen <= reach && at < slice.size; ++at) {
                if (std::memcmp(slice.data + at, kKey, kKeyLen) != 0) continue;
                const uintptr_t addr = slice.address + at;
                uint64_t tail[2] = {};
                if (!engine::Read(proc, addr + 0x10, tail, sizeof tail) || tail[0] != kKeyLen || tail[1] != 15) continue;
                if (gammaScan.keys.size() < 16) gammaScan.keys.push_back(addr);
            }
        } else if (gammaScan.pass == 3) {
            for (size_t at = 4; at + sizeof kGammaTail <= reach && at < slice.size; ++at) {
                if (std::memcmp(slice.data + at, kGammaTail, sizeof kGammaTail) != 0) continue;
                const uintptr_t valueAddr = slice.address + at - 4;
                if (valueAddr < 0x10000ull + optrec::kValue) continue;
                const uintptr_t record = valueAddr - optrec::kValue;
                float value = 0, fallback = 0;
                if (!GammaRecordLooksRight(record, value, fallback) || gammaRecords.size() >= 16) continue;
                if (std::find(gammaRecords.begin(), gammaRecords.end(), record) == gammaRecords.end())
                    gammaRecords.push_back(record);
            }
        } else {
            for (size_t at = (8u - (slice.address & 7u)) & 7u; at + 8 <= reach && at < slice.size; at += 8) {
                uint64_t pointer = 0;
                std::memcpy(&pointer, slice.data + at, sizeof pointer);
                uintptr_t nearest = kDescriptorSpan;
                for (uintptr_t key : gammaScan.keys)
                    if (pointer <= key && key - pointer < nearest) nearest = key - pointer;
                if (nearest >= kDescriptorSpan) continue;
                const uintptr_t record = slice.address + at - kDescriptor;
                float value = 0, fallback = 0;
                if (GammaRecordLooksRight(record, value, fallback) && gammaCandidates.size() < 32)
                    gammaCandidates.push_back({nearest, record});
            }
        }
    }
    if (gammaScan.cursor < 0x7FFFFFFF0000ull) return;
    gammaScan.cursor = 0;
    if (gammaScan.pass == 1) {
        gammaScan.pass = gammaScan.keys.empty() ? 3 : 2;
        return;
    }
    if (gammaScan.pass == 2) {
        uintptr_t nearest = kDescriptorSpan;
        for (const auto& c : gammaCandidates) nearest = std::min(nearest, c.first);
        for (const auto& c : gammaCandidates)
            if (c.first == nearest && gammaRecords.size() < 4) gammaRecords.push_back(c.second);
        gammaCandidates.clear();
        gammaScan.pass = 3;
        return;
    }
    gammaScan.pass = 4;
    gammaScan.scratch.clear();
    gammaScan.scratch.shrink_to_fit();
    Log("[fullbright] %d brightness record(s)", (int)gammaRecords.size());
}

void Backend::Impl::RestoreGamma() {
    if (gammaPinned && proc.ok())
        for (size_t i = 0; i < gammaRecords.size() && i < gammaOriginal.size(); ++i)
            engine::Write(proc, gammaRecords[i] + optrec::kValue, gammaOriginal[i]);
    gammaPinned = false;
}

void Backend::Impl::FullbrightTick(const Modules& m) {
    const bool on = On(m, "fullbright");
    if (on && gammaScan.pass == 0) {
        gammaScan.pass = 1;
        gammaScan.cursor = 0;
    }
    if (on && !gammaWasOn && gammaScan.pass == 4 && gammaRecords.empty() && gammaRetries < 3) {
        ++gammaRetries;
        gammaScan = GammaScan{};
        gammaScan.pass = 1;
        gammaCandidates.clear();
    }
    gammaWasOn = on;
    GammaScanStep();
    if (gammaRecords.empty()) return;
    if (!on) {
        RestoreGamma();
        return;
    }
    const auto now = clock_t_::now();
    if (gammaPinned && now < gammaNextEnforce) return;
    gammaNextEnforce = now + std::chrono::milliseconds(100);
    if (gammaPinned && gammaOriginal.size() < gammaRecords.size()) RestoreGamma();
    if (!gammaPinned) {
        gammaOriginal.clear();
        for (uintptr_t record : gammaRecords) {
            float value = 0, fallback = 0.5f;
            GammaRecordLooksRight(record, value, fallback);
            gammaOriginal.push_back(fallback);
        }
        gammaPinned = true;
    }
    for (uintptr_t record : gammaRecords) {
        float live = 0;
        if (!engine::Read(proc, record + optrec::kValue, live) || std::fabs(live - kGamma) > 0.01f)
            engine::Write(proc, record + optrec::kValue, kGamma);
    }
}

}  // namespace citron
