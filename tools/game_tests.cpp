// The build table: versions map to the right build, every site is well formed, and the bytes the
// client changes are wildcards in the patterns that find them, so a game left patched still matches.
#include "citron/game.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace citron::game;

namespace {

int g_failed = 0;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++g_failed;
    }
}

// -1 for a wildcard, the byte otherwise; empty when the text is not a pattern.
std::vector<int> Parse(const char* text) {
    std::vector<int> out;
    const std::string s = text ? text : "";
    for (size_t i = 0; i < s.size();) {
        if (s[i] == ' ') {
            ++i;
            continue;
        }
        if (i + 1 >= s.size()) return {};
        const std::string token = s.substr(i, 2);
        if (i + 2 < s.size() && s[i + 2] != ' ') return {};
        if (token == "??") {
            out.push_back(-1);
        } else if (std::isxdigit((unsigned char)token[0]) && std::isxdigit((unsigned char)token[1])) {
            out.push_back((int)std::strtol(token.c_str(), nullptr, 16));
        } else {
            return {};
        }
        i += 2;
    }
    return out;
}

const char* const kNames[] = {"GameCore", "Camera", "HurtTime", "Looking", "Reach", "ReachWall",
                              "SelectSlot", "AirAccel", "JumpDelay", "HitboxWidth", "UseDelay", "OwnNametag",
                              "Perspective", "FovLimit", "TurnCall", "MouseInput", "GrabCursor", "ReleaseCursor"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == (size_t)Site::Count, "a site without a name here");

void Wild(Build b, Site s, std::initializer_list<int> offsets) {
    const SiteDef& d = SiteFor(b, s);
    const std::vector<int> p = Parse(d.pattern);
    for (int o : offsets)
        Require(o < (int)p.size() && p[o] == -1, std::string(BuildName(b)) + " " + d.name + ": byte +" +
                                                     std::to_string(o) + " is changed by the client, so it must be ??");
}

}  // namespace

int main() {
    // versions, as the package and the file report them
    Require(BuildFromVersion("1.26.52.3") == Build::V1_26_52, "1.26.52.3");
    Require(BuildFromVersion("1.26.5203.0") == Build::V1_26_52, "the Store's 1.26.5203.0");
    Require(BuildFromVersion("1.26.60.5") == Build::V1_26_60, "1.26.60.5");
    Require(BuildFromVersion("1.26.6005.0") == Build::V1_26_60, "the Store's 1.26.6005.0");
    Require(BuildFromVersion("1.26.52.7") == Build::V1_26_52, "a hotfix of 1.26.52 is still tried as 1.26.52");
    Require(BuildFromVersion("1.26.51.1") == Build::Unknown, "1.26.51 is not supported");
    Require(BuildFromVersion("1.21.130.4") == Build::Unknown, "1.21.130 is not supported");
    Require(BuildFromVersion("1.26.52") == Build::Unknown, "three parts are not a version");
    Require(BuildFromVersion("") == Build::Unknown, "an empty version");
    Require(BuildFromVersion("1.26.x.0") == Build::Unknown, "a version with letters");
    Require(BuildFromVersion("1.26.52.3.1") == Build::Unknown, "five parts are not a version");
    Require(std::strcmp(BuildName(Build::V1_26_52), "1.26.52") == 0 &&
                std::strcmp(BuildName(Build::V1_26_60), "1.26.60") == 0,
            "build names");

    // every site of both builds
    for (Build b : {Build::V1_26_52, Build::V1_26_60}) {
        const std::string tag = BuildName(b);
        for (size_t i = 0; i < (size_t)Site::Count; ++i) {
            const SiteDef& d = SiteFor(b, (Site)i);
            Require(d.name && std::strcmp(d.name, kNames[i]) == 0, tag + ": site " + std::to_string(i) + " is out of order");
            if (!d.pattern) {
                Require(!d.required && d.rva == 0, tag + " " + kNames[i] + ": a site without a pattern");
                continue;
            }
            const std::vector<int> p = Parse(d.pattern);
            Require(p.size() >= 6, tag + " " + kNames[i] + ": the pattern does not parse or is too short");
            Require(d.rva != 0, tag + " " + kNames[i] + ": no address");
            int fixed = 0;
            for (int v : p) fixed += v >= 0;
            Require(fixed >= 4, tag + " " + kNames[i] + ": too few fixed bytes to be unique");
        }
        for (Site s : {Site::GameCore, Site::Camera})
            Require(SiteFor(b, s).required, tag + " " + kNames[(size_t)s] + " is required");
        Wild(b, Site::JumpDelay, {5, 6});
        Wild(b, Site::UseDelay, {20, 21});
        Wild(b, Site::OwnNametag, {0, 1});
        Wild(b, Site::FovLimit, {4, 5, 6, 7, 12, 13, 14, 15});
        Require(Parse(SiteFor(b, Site::TurnCall).pattern)[(size_t)LayoutFor(b).turnCallAt] == 0xE8, tag + ": the turn's call is where the layout says");
        Require(LayoutFor(b).turnDelta != 0, tag + ": the turn's function");
    }
    Wild(Build::V1_26_52, Site::Reach, {0, 1, 4, 6, 7, 8, 9});
    Wild(Build::V1_26_60, Site::Reach, {8, 9, 10, 11, 18, 19, 20, 21});
    Wild(Build::V1_26_60, Site::ReachWall, {0xB, 0xC});
    Require(!SiteFor(Build::V1_26_52, Site::ReachWall).pattern, "1.26.52 has no separate wall test");
    Require(SiteFor(Build::V1_26_52, Site::Camera).rva != SiteFor(Build::V1_26_60, Site::Camera).rva,
            "the two builds share an address");
    Require(!SiteFor(Build::Unknown, Site::Camera).pattern, "an unknown build has sites");

    // the layouts
    const Layout& a = LayoutFor(Build::V1_26_52);
    const Layout& b = LayoutFor(Build::V1_26_60);
    Require(a.ownNametagSkip[0] == 0x74 && a.ownNametagSkip[1] == 0xDA, "1.26.52's own nametag je");
    Require(b.ownNametagSkip[0] == 0x74 && b.ownNametagSkip[1] == 0xDB, "1.26.60's own nametag je");
    for (const Layout* l : {&a, &b}) {
        const uintptr_t fields[] = {l->coreMinecraftGame, l->gameClientMap, l->clientMinecraftGame, l->clientMinecraft,
                                    l->minecraftTimer, l->clientLocalPlayer, l->actorRegistry, l->actorEntity,
                                    l->actorTypeId, l->actorStateVector, l->actorAabb, l->actorRotation,
                                    l->actorNameTag, l->playerSupplies};
        for (uintptr_t f : fields) Require(f != 0 && f < 0x2000 && (f & 7) == 0, "a layout offset looks wrong");
    }
    Require(a.actorStateVector != b.actorStateVector, "the two layouts are the same");
    Require(LayoutFor(Build::Unknown).actorStateVector == 0, "an unknown build has a layout");

    if (g_failed) {
        std::cerr << g_failed << " checks failed\n";
        return 1;
    }
    std::cout << "game table tests passed\n";
    return 0;
}
