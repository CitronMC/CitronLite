// Pattern search over a game image, a Boyer-Moore-Horspool scan of its executable sections.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace citron::sigscan {

struct MatchSet {
    std::vector<uintptr_t> addresses;
    bool validImage = false;

    bool exact(size_t expectedHits) const noexcept {
        return validImage && expectedHits > 0 && addresses.size() == expectedHits;
    }
};

namespace detail {

inline const IMAGE_NT_HEADERS64* NtHeaders(const uint8_t* image, size_t imageSize) noexcept {
    if (!image || imageSize < sizeof(IMAGE_DOS_HEADER)) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) return nullptr;
    const size_t ntOffset = static_cast<size_t>(dos->e_lfanew);
    if (ntOffset > imageSize || sizeof(IMAGE_NT_HEADERS64) > imageSize - ntOffset) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + ntOffset);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    const auto* sectionBytes = reinterpret_cast<const uint8_t*>(sections);
    const size_t sectionOffset = static_cast<size_t>(sectionBytes - image);
    const size_t sectionCount = nt->FileHeader.NumberOfSections;
    if (sectionOffset > imageSize ||
        sectionCount > (imageSize - sectionOffset) / sizeof(IMAGE_SECTION_HEADER)) return nullptr;
    return nt;
}

inline bool PatternAt(const uint8_t* start, const std::vector<int>& pattern) noexcept {
    for (size_t index = 0; index < pattern.size(); ++index) {
        const int expected = pattern[index];
        if (expected >= 0 && start[index] != static_cast<uint8_t>(expected)) return false;
    }
    return true;
}

struct Anchor {
    size_t offset = 0;
    size_t length = 0;
};

inline Anchor StrongestAnchor(const std::vector<int>& pattern) noexcept {
    Anchor best{};
    for (size_t begin = 0; begin < pattern.size();) {
        while (begin < pattern.size() && pattern[begin] < 0) ++begin;
        size_t end = begin;
        while (end < pattern.size() && pattern[end] >= 0) ++end;
        if (end - begin > best.length) best = {begin, end - begin};
        begin = end;
    }
    return best;
}

inline void ScanRange(const uint8_t* image, size_t begin, size_t end, uintptr_t imageBase,
                      const std::vector<int>& pattern, const Anchor& anchor,
                      size_t stopAfter, std::vector<uintptr_t>& matches) {
    if (begin > end || pattern.size() > end - begin || anchor.length == 0) return;

    std::array<size_t, 256> shifts{};
    shifts.fill(anchor.length);
    for (size_t index = 0; index + 1 < anchor.length; ++index)
        shifts[static_cast<uint8_t>(pattern[anchor.offset + index])] = anchor.length - 1 - index;

    const size_t lastCandidate = end - pattern.size();
    size_t candidate = begin;
    while (candidate <= lastCandidate) {
        const size_t anchorStart = candidate + anchor.offset;
        const uint8_t tail = image[anchorStart + anchor.length - 1];
        if (tail == static_cast<uint8_t>(pattern[anchor.offset + anchor.length - 1])) {
            bool anchorMatches = true;
            for (size_t index = 0; index < anchor.length; ++index) {
                if (image[anchorStart + index] !=
                    static_cast<uint8_t>(pattern[anchor.offset + index])) {
                    anchorMatches = false;
                    break;
                }
            }
            if (anchorMatches && PatternAt(image + candidate, pattern)) {
                matches.push_back(imageBase + candidate);
                if (stopAfter && matches.size() >= stopAfter) return;
            }
        }
        const size_t shift = shifts[tail];
        candidate += std::max<size_t>(shift, 1);
    }
}

} // namespace detail

// Matches in the executable sections of a PE image laid out as loaded (what engine::ReadModule
// gives). `stopAfter` ends the scan early: asking for two tells a unique pattern from an ambiguous one.
inline MatchSet FindExecutableMatches(const uint8_t* image, size_t imageSize, uintptr_t imageBase,
                                      const std::vector<int>& pattern, size_t stopAfter = 0) {
    MatchSet result;
    const auto* nt = detail::NtHeaders(image, imageSize);
    if (!nt || pattern.empty()) return result;
    const auto anchor = detail::StrongestAnchor(pattern);
    if (!anchor.length) return result;
    result.validImage = true;

    const auto* sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const auto& section = sections[index];
        if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const size_t begin = section.VirtualAddress;
        const size_t virtualSize = std::max<size_t>(section.Misc.VirtualSize,
                                                    section.SizeOfRawData);
        if (begin >= imageSize) continue;
        const size_t end = begin + std::min(virtualSize, imageSize - begin);
        detail::ScanRange(image, begin, end, imageBase, pattern, anchor, stopAfter,
                          result.addresses);
        if (stopAfter && result.addresses.size() >= stopAfter) break;
    }
    return result;
}

} // namespace citron::sigscan
