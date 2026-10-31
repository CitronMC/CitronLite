#include "citron/game.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <appmodel.h>
#include <winver.h>
#include <array>
#include <charconv>
#include <vector>

namespace citron::game {
namespace {

constexpr size_t kSites = static_cast<size_t>(Site::Count);
using SiteTable = std::array<SiteDef, kSites>;

// Both tables are in Site order. Every pattern is unique in its own build's image.
constexpr SiteTable k52{{
    {"GameCore", true, 0x7D8F2, "4C 89 3D ?? ?? ?? ?? 4D 85 FF"},
    {"Camera", true, 0x66EE404, "F3 41 0F 58 40 04 F3 0F"},
    {"HurtTime", false, 0x46BD5C7, "8B 28 48 89 ?? 48 83 C1"},
    {"Looking", false, 0x4BCBCA, "8B 40 ?? 83 C0 ?? 0F 28"},
    {"Reach", false, 0x4BCC4A,
     "?? ?? F3 0F ?? 3D ?? ?? ?? ?? 40 B6 01 EB ?? 40 B6 01 0F 2E 3D ?? ?? ?? ?? 76 ?? F3 0F 10 3D"},
    {"ReachWall", false, 0, nullptr},
    {"SelectSlot", false, 0x1F4860,
     "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 88 02 00 00 48 8D AC 24 80 00 00 00 48 C7 85 00 02 00 00 FE FF FF FF "
     "89 D6 83 FA 08 0F 87"},
    {"AirAccel", false, 0x2E118DB, "81 F3 41 0F 11 40 0C"},
    {"JumpDelay", false, 0x95EAFCD, "?? 83 7E ?? ?? ?? ?? ?? ?? ?? ?? 4C 89 F1 48 89 F2"},
    {"HitboxWidth", false, 0x1F80CB, "F3 0F 10 40 18 48 83 C4 20 5E C3 CC CC CC CC CC CC CC CC CC CC"},
    {"UseDelay", false, 0x5D95D3A, "48 81 C2 00 C2 EB 0B 48 8B 06 48 8B 80 D0 0B 00 00 48 89 F1 ?? ??"},
    {"OwnNametag", false, 0x46B3204,
     "?? ?? 48 8B 03 48 8B 80 ?? ?? ?? ?? 48 89 D9 FF 15 ?? ?? ?? ?? 84 C0 75 ?? 48 8B 4B"},
    {"Perspective", false, 0xFB7430,
     "48 83 EC 38 48 8B 05 ?? ?? ?? ?? 48 31 E0 48 89 44 24 ?? 48 8B 01 48 8B 40 08 48 8D 54 24 ?? "
     "41 B8 03 00 00 00"},
    {"FovLimit", false, 0x46BD4A9, "F3 0F 5F 05 ?? ?? ?? ?? F3 0F 10 0D ?? ?? ?? ?? 0F 28 F1 F3 0F C2 F7 01"},
    {"TurnCall", false, 0x4761834, "F3 45 0F 59 C1 F3 44 0F 11 44 24 24 48 8D 54 24 20 48 89 F1 E8 ?? ?? ?? ?? 48 8B 4C 24 28 48 31 E1"},
    {"MouseInput", false, 0x8A880,
     "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ?? ?? ?? ?? 48 8D AC 24 ?? ?? ?? ?? 44 0F 29 BD ?? ?? ?? ?? 44 0F 29 B5 ?? ?? ?? ?? 44 0F 29 AD ?? ?? ?? ?? 44 0F 29 A5 ?? ?? ?? ?? 44 0F 29 9D ?? ?? ?? ?? 44 0F 29 95 ?? ?? ?? ?? 44 0F 29 8D ?? ?? ?? ?? 44 0F 29 85 ?? ?? ?? ?? 0F 29 BD ?? ?? ?? ?? 0F 29 B5 ?? ?? ?? ?? 48 C7 85 ?? ?? ?? ?? ?? ?? ?? ?? 48 89 CE 8B 05"},
    {"GrabCursor", false, 0x5DCE3C0,
     "56 48 83 EC ?? 48 89 CE 48 8B 01 48 8B 80 ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 84 C0 74 ?? 48 8B 8E ?? ?? ?? ?? 48 8B 01 48 8B 80 ?? ?? ?? ?? 48 8B 15 ?? ?? ?? ?? 48 83 C4 ?? 5E 48 FF E2 90 48 83 C4 ?? 5E C3 CC CC CC CC CC CC CC CC CC CC CC CC CC 56 48 83 EC"},
    {"ReleaseCursor", false, 0x5DCE410,
     "56 48 83 EC ?? 48 89 CE 48 8B 01 48 8B 80 ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 84 C0 74 ?? 48 8B 8E ?? ?? ?? ?? 48 8B 01 48 8B 80 ?? ?? ?? ?? 48 8B 15 ?? ?? ?? ?? 48 83 C4 ?? 5E 48 FF E2 90 48 83 C4 ?? 5E C3 CC CC CC CC CC CC CC CC CC CC CC CC CC 56 53"},
}};

constexpr SiteTable k60{{
    {"GameCore", true, 0x7DBF2, "4C 89 3D ?? ?? ?? ?? 4D 85 FF"},
    {"Camera", true, 0x6EEBA64, "F3 41 0F 58 40 04 F3 0F"},
    {"HurtTime", false, 0x53A1030, "8B 28 48 89 ?? 48 83 C1"},
    {"Looking", false, 0x4E0A2EB, "8B 40 18 83 C0 FC 44 0F 28 C6 83 F8 FD"},
    {"Reach", false, 0x4E0A2C1,
     "84 C0 75 0A F3 0F 10 3D ?? ?? ?? ?? EB 08 F3 0F 10 3D ?? ?? ?? ?? 48 8B 03 48 8B 80 ?? ?? ?? ?? "
     "48 89 D9"},
    {"ReachWall", false, 0x4E0A391, "44 0F 2E C7 48 8B 8D ?? ?? ?? ?? ?? ?? 40 B6 01 44 0F 28 C7 EB 02 31 F6"},
    {"SelectSlot", false, 0x2CD380,
     "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 88 02 00 00 48 8D AC 24 80 00 00 00 48 C7 85 00 02 00 00 FE FF FF FF "
     "45 84 C0 75 6C 83 FA 08 77 67"},
    {"AirAccel", false, 0x336F02B, "81 F3 41 0F 11 40 0C"},
    {"JumpDelay", false, 0xAD34A55, "?? 83 7E ?? ?? ?? ?? ?? ?? ?? ?? 4C 89 F1 48 89 F2"},
    {"HitboxWidth", false, 0x2D0DAB, "F3 0F 10 40 18 48 83 C4 20 5E C3 CC CC CC CC CC CC CC CC CC CC"},
    // Five copies of the use delay end like this on 1.26.60; the tail after the call picks out the
    // one that follows an attack.
    {"UseDelay", false, 0x6585BD2,
     "48 81 C2 00 C2 EB 0B 48 8B 06 48 8B 80 B0 0B 00 00 48 89 F1 ?? ?? ?? ?? ?? ?? 90 48 8B 06 48 8B 80 "
     "B8 09 00 00 48 89 F1 FF 15 ?? ?? ?? ?? 90 84"},
    {"OwnNametag", false, 0x5397AB3,
     "?? ?? 48 8B 03 48 8B 80 ?? ?? ?? ?? 48 89 D9 FF 15 ?? ?? ?? ?? 90 84 C0 75 ?? 48 8B 4B"},
    {"Perspective", false, 0x5AFB0E0,
     "48 83 EC 38 48 8B 05 ?? ?? ?? ?? 48 31 E0 48 89 44 24 ?? 48 8B 01 48 8B 40 08 48 8D 54 24 ?? "
     "41 B8 03 00 00 00"},
    {"FovLimit", false, 0x5309D99, "F3 0F 5F 05 ?? ?? ?? ?? F3 0F 10 0D ?? ?? ?? ?? 0F 28 F1 F3 0F C2 F7 01"},
    {"TurnCall", false, 0x6DA4B72, "45 0F 14 C0 44 0F 59 C7 44 0F 13 44 24 20 48 8D 54 24 20 48 89 F1 E8 ?? ?? ?? ?? 48 8B 44 24 28"},
    {"MouseInput", false, 0x8A450,
     "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC E8 02 00 00 48 8D AC 24 80 00 00 00 44 0F 29 AD 50 02 00 00 44 0F 29 A5 40 02 00 00 44 0F 29 9D 30 02 00 00 44 0F 29 95 20 02 00 00 44 0F 29 8D 10 02 00 00 44 0F 29 85 00 02 00 00 0F 29 BD F0 01 00 00 0F 29 B5 E0 01 00 00 48 C7 85 D8 01 00 00 FE FF FF FF 48 89 CE"},
    {"GrabCursor", false, 0x65BB0A0,
     "56 48 83 EC 20 48 89 CE 48 8B 01 48 8B 80 00 05 00 00 FF 15 ?? ?? ?? ?? 84 C0 74 20 48 8B 8E C8 01 00 00 48 8B 01 48 8B 80 38 04 00 00"},
    {"ReleaseCursor", false, 0x65BB0F0,
     "56 48 83 EC 20 48 89 CE 48 8B 01 48 8B 80 00 05 00 00 FF 15 ?? ?? ?? ?? 84 C0 74 20 48 8B 8E C8 01 00 00 48 8B 01 48 8B 80 40 04 00 00"},
}};

constexpr SiteTable kNone{};

constexpr Layout kLayout52{
    {0x74, 0xDA},
    0x48, 0x970, 0x1A8, 0x1B0, 0xE0, 0x248,
    0x10, 0x18, 0x1E8, 0x218, 0x220, 0x228, 0xBC0,
    0x5B8,
    20, 0x4761890,
};

constexpr Layout kLayout60{
    {0x74, 0xDB},
    0x48, 0x9D0, 0x1C8, 0x1D0, 0xF0, 0x268,
    0x10, 0x18, 0x210, 0x240, 0x248, 0x250, 0xBE8,
    0x5E0,
    22, 0x6DA4BD0,
};

constexpr Layout kNoLayout{};

bool ParseVersion(std::string_view text, std::array<uint32_t, 4>& parts) {
    std::array<uint32_t, 4> out{};
    size_t at = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        const size_t dot = text.find('.', at);
        const bool last = i + 1 == out.size();
        if (last != (dot == std::string_view::npos)) return false;
        const std::string_view part = text.substr(at, (last ? text.size() : dot) - at);
        if (part.empty() || part.size() > 6) return false;
        const auto r = std::from_chars(part.data(), part.data() + part.size(), out[i]);
        if (r.ec != std::errc{} || r.ptr != part.data() + part.size()) return false;
        at = dot + 1;
    }
    parts = out;
    return true;
}

bool PackageVersion(HANDLE process, std::array<uint32_t, 4>& parts) {
    UINT32 bytes = 0;
    if (GetPackageId(process, &bytes, nullptr) != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(PACKAGE_ID) ||
        bytes > 1024u * 1024u)
        return false;
    std::vector<uint8_t> buffer(bytes);
    if (GetPackageId(process, &bytes, buffer.data()) != ERROR_SUCCESS) return false;
    const auto* id = reinterpret_cast<const PACKAGE_ID*>(buffer.data());
    parts = {id->version.Major, id->version.Minor, id->version.Build, id->version.Revision};
    return true;
}

bool FileVersion(HANDLE process, std::array<uint32_t, 4>& parts) {
    std::vector<wchar_t> path(32768);
    DWORD chars = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &chars) || !chars) return false;
    DWORD ignored = 0;
    const DWORD bytes = GetFileVersionInfoSizeW(path.data(), &ignored);
    if (!bytes || bytes > 16u * 1024u * 1024u) return false;
    std::vector<uint8_t> info(bytes);
    if (!GetFileVersionInfoW(path.data(), 0, bytes, info.data())) return false;
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT fixedBytes = 0;
    if (!VerQueryValueW(info.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixedBytes) || !fixed ||
        fixedBytes < sizeof(VS_FIXEDFILEINFO) || fixed->dwSignature != 0xFEEF04BDu)
        return false;
    parts = {HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS), HIWORD(fixed->dwFileVersionLS),
             LOWORD(fixed->dwFileVersionLS)};
    return true;
}

}  // namespace

Build BuildFromVersion(std::string_view version) {
    std::array<uint32_t, 4> v{};
    if (!ParseVersion(version, v)) return Build::Unknown;
    // The Store reports 1.26.60.5 as 1.26.6005.0.
    if (v[2] >= 100 && v[3] == 0) {
        v[3] = v[2] % 100;
        v[2] /= 100;
    }
    if (v[0] != 1 || v[1] != 26) return Build::Unknown;
    if (v[2] == 52) return Build::V1_26_52;
    if (v[2] == 60) return Build::V1_26_60;
    return Build::Unknown;
}

const char* BuildName(Build build) {
    switch (build) {
    case Build::V1_26_52: return "1.26.52";
    case Build::V1_26_60: return "1.26.60";
    default: return "unknown";
    }
}

const SiteDef& SiteFor(Build build, Site site) {
    const size_t i = static_cast<size_t>(site);
    if (i >= kSites) return kNone[0];
    switch (build) {
    case Build::V1_26_52: return k52[i];
    case Build::V1_26_60: return k60[i];
    default: return kNone[i];
    }
}

const Layout& LayoutFor(Build build) {
    switch (build) {
    case Build::V1_26_52: return kLayout52;
    case Build::V1_26_60: return kLayout60;
    default: return kNoLayout;
    }
}

bool ReadVersion(uint32_t processId, std::string& out) {
    out.clear();
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return false;
    std::array<uint32_t, 4> v{};
    const bool ok = PackageVersion(process, v) || FileVersion(process, v);
    CloseHandle(process);
    if (!ok) return false;
    out = std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]) + "." + std::to_string(v[3]);
    return true;
}

}  // namespace citron::game
