#include "../../backend/backend_impl.h"
#include "citron/pe_signature_scan.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace citron {
namespace {

// Clients whose reach readout this can rewrite, and the store each one writes that number through.
enum class Match { NameHas, VersionDll };

struct SpoofTarget {
    const char* client;
    Match match;
    const char* hint;
    const char* pattern;
    int stolen;
    bool takeFirst;   // the pattern may repeat; the first hit is the store
};

const SpoofTarget kTargets[] = {
    {"Flarial", Match::NameHas, "flarial", "F3 0F 11 83 70 13 00 00 48 8D 4C", 8, false},   // movss [rbx+0x1370], xmm0
    {"oderso", Match::VersionDll, "", "F3 0F 11 86 48 01 00 00", 8, true},                  // movss [rsi+0x148], xmm0
};

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// oderso's module is named after the game's version, like 1.26.45.dll.
bool ModuleMatches(const std::string& name, const SpoofTarget& t) {
    const std::string low = Lower(name);
    if (t.match == Match::NameHas) return low.find(t.hint) != std::string::npos;
    return low.size() > 4 && std::isdigit((unsigned char)low.front()) && low.rfind(".dll") == low.size() - 4 &&
           low.find('.') < low.size() - 4;
}

}  // namespace

void Backend::Impl::RemoveReachSpoofer() {
    if (spoofHook.installed && proc.ok()) engine::RemoveHook(proc, spoofHook);
    spoofHook = engine::CaveHook{};
    spoofMultWrote = spoofCapWrote = -1.f;
    spoofModuleBase = 0;
}

// The number another client's reach display shows becomes min(value * multiplier, cap). Their real
// reach and the game are untouched.
void Backend::Impl::ReachSpooferTick(const Modules& m) {
    const ModuleSnapshot* s = Mod(m, "reachspoofer");
    if (!s || !s->on || !proc.ok()) {
        if (spoofHook.installed || spoofModuleBase) RemoveReachSpoofer();
        return;
    }
    const float mult = std::clamp(Num(*s, "multiplier", 100.f), 0.f, 500.f) / 100.f;
    const float cap = std::clamp(Num(*s, "cap", 3.f), 0.f, 64.f);
    if (spoofHook.installed) {
        if (std::fabs(mult - spoofMultWrote) > 1e-6f && engine::Write(proc, spoofHook.slot, mult)) spoofMultWrote = mult;
        if (std::fabs(cap - spoofCapWrote) > 1e-6f && engine::Write(proc, spoofHook.slot + 4, cap)) spoofCapWrote = cap;
        return;
    }
    const auto now = clock_t_::now();
    if (now < spoofNextScan) return;
    spoofNextScan = now + std::chrono::milliseconds(1500);

    const std::vector<engine::ModuleInfo> mods = engine::EnumModules(proc.pid);
    for (const SpoofTarget& t : kTargets) {
        const std::vector<int> pattern = engine::ParsePattern(t.pattern);
        for (const engine::ModuleInfo& mod : mods) {
            if (mod.system || !mod.base || !mod.size || mod.base == (uintptr_t)proc.base) continue;
            if (!ModuleMatches(mod.name, t)) continue;
            std::vector<uint8_t> image;
            if (!engine::ReadRegion(proc, mod.base, mod.size, image)) continue;
            const auto hits = sigscan::FindExecutableMatches(image.data(), image.size(), mod.base, pattern, 8);
            if (hits.addresses.empty() || (hits.addresses.size() > 1 && !t.takeFirst)) continue;
            const uintptr_t at = hits.addresses.front();
            if (!engine::InstallXmm0CapHook(proc, spoofHook, at, t.stolen, mult, cap)) continue;
            spoofMultWrote = mult;
            spoofCapWrote = cap;
            spoofModuleBase = mod.base;
            Log("[reachspoof] %s's reach display in %s", t.client, mod.name.c_str());
            return;
        }
    }
}

}  // namespace citron
