// Telling the game's FOV option record from bytes that merely look like one. The value is found by
// a short pattern (a default of 60.0 and a step of 0.001) that also matches inside private buffers,
// so a match only counts when the whole record reads like the option. A pure function over the
// record's bytes, so the tests can use records read out of a running game.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace citron::optrec {

// Bedrock's float option record: a vtable, then the numbers the options screen is built from.
inline constexpr size_t kVTable = 0x00;
inline constexpr size_t kMin = 0x10, kMax = 0x14, kValue = 0x18, kDefault = 0x1C, kStep = 0x20;
inline constexpr size_t kBytes = 0x30;   // how much of a record has to be read to judge it

// `raw` holds the first kBytes of the candidate, read from `record`. A real option's vtable points
// into Minecraft's own image.
inline bool LooksLikeFovOption(const uint8_t* raw, uint64_t record, uint64_t imageBase,
                               uint64_t imageSize, float* valueOut = nullptr) {
    // an object that starts with a pointer is 8-aligned; the false matches seen in the game were not
    if (record & 7u) return false;

    uint64_t vtable = 0;
    std::memcpy(&vtable, raw + kVTable, sizeof(vtable));
    if (!imageBase || vtable < imageBase || vtable >= imageBase + imageSize) return false;

    float mn = 0, mx = 0, value = 0, dflt = 0, step = 0;
    std::memcpy(&mn,    raw + kMin,     4);
    std::memcpy(&mx,    raw + kMax,     4);
    std::memcpy(&value, raw + kValue,   4);
    std::memcpy(&dflt,  raw + kDefault, 4);
    std::memcpy(&step,  raw + kStep,    4);
    for (const float f : { mn, mx, value, dflt, step })
        if (!std::isfinite(f)) return false;

    // The slider's bounds, loosely (30 to 110 today). The value itself may sit outside them: Zoom and
    // Custom FOV write numbers the slider cannot.
    if (mn < 5.f || mn > 60.f) return false;
    if (mx < 90.f || mx > 180.f) return false;
    if (value <= 1.f || value >= 179.f) return false;
    if (dflt < mn || dflt > mx) return false;
    if (step < 1.0e-5f || step > 1.0f) return false;

    if (valueOut) *valueOut = value;
    return true;
}

} // namespace citron::optrec
