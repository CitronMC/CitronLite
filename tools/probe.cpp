// Prints what Citron Lite reads from a running Minecraft without writing anything to it: the build,
// every site, and the walk to your player. The first thing to run after a game update.
//   ./build_mingw.sh probe
//   ./build_mingw.sh probe --live    also puts the camera and actor captures in for two seconds and
//                                    lists what they saw, then takes them out again
#include "citron/bedrock.h"
#include "citron/engine.h"
#include "citron/game.h"
#include "citron/game_bindings.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace citron;

namespace {

void Live(const engine::Proc& proc, game::Build build, bedrock::Reader& rd, uintptr_t me) {
    const uintptr_t base = (uintptr_t)proc.base;
    engine::CaveHook cam, ring;
    const bool camOk = engine::InstallCaptureR8Hook(proc, cam, base + game::SiteFor(build, game::Site::Camera).rva, 6);
    const bool ringOk = engine::InstallRingHook(proc, ring, base + game::SiteFor(build, game::Site::ActorRender).rva + 0x2E,
                                                6, 0x30, 1024);
    std::printf("live: camera capture %s, actor ring %s\n", camOk ? "in" : "FAILED", ringOk ? "in" : "FAILED");
    Sleep(2000);
    if (camOk) {
        uintptr_t at = 0;
        float yp[2] = {};
        if (engine::Read(proc, cam.slot, at) && at && engine::Read(proc, at, yp, sizeof yp)) {
            float pitch = 0, yaw = 0;
            rd.ReadRotation(me, pitch, yaw);
            std::printf("  camera 0x%llX: yaw %.4f, pitch %.4f rad (%.2f, %.2f degrees); the player turns %.2f, %.2f\n",
                        (unsigned long long)at, yp[0], yp[1], yp[0] * 57.29578f, yp[1] * 57.29578f, yaw, pitch);
        } else {
            std::printf("  the camera was not captured (is the game in the foreground and drawing?)\n");
        }
    }
    if (ringOk) {
        uint64_t writes = 0;
        engine::Read(proc, ring.slot, writes);
        std::vector<uintptr_t> slots(ring.ringN);
        engine::Read(proc, ring.slot + 8, slots.data(), slots.size() * sizeof(uintptr_t));
        std::set<uintptr_t> actors(slots.begin(), slots.end());
        actors.erase(0);
        std::printf("  %llu actors drawn, %zu different ones in the ring\n", (unsigned long long)writes, actors.size());
        int shown = 0;
        for (uintptr_t a : actors) {
            if (shown++ >= 24) break;
            uintptr_t vt = 0;
            engine::Read(proc, a, vt);
            std::string type, tag;
            rd.TypeId(a, type);
            rd.NameTag(a, tag);
            float mn[3] = {}, mx[3] = {};
            const bool box = rd.ReadBox(a, mn, mx);
            bedrock::Motion mo;
            const bool moved = rd.ReadMotion(a, mo);
            std::printf("    %s vt +0x%llX %-28.28s %-18.18s", a == me ? "me " : "   ",
                        (unsigned long long)(vt - base), type.c_str(), tag.c_str());
            if (box) std::printf(" box %.2f x %.2f at %.1f %.1f %.1f", mx[0] - mn[0], mx[1] - mn[1], mn[0], mn[1], mn[2]);
            if (moved && box) std::printf(" eye %.2f above", mo.pos[1] - mn[1]);
            std::printf("\n");
        }
    }
    if (camOk) engine::RemoveHook(proc, cam);
    if (ringOk) engine::RemoveHook(proc, ring);
    std::printf("live: captures out\n");
}

}  // namespace

int main(int argc, char** argv) {
    const bool live = argc > 1 && std::strcmp(argv[1], "--live") == 0;
    engine::Proc proc;
    if (!engine::Attach(proc, "Minecraft.Windows.exe")) {
        std::printf("Minecraft is not running\n");
        return 1;
    }
    const gamebind::Bindings b = gamebind::Resolve(proc.h);
    std::printf("bindings%s: attack %d, use %d, forward %d, back %d, left %d, right %d, jump %d, sneak %d, sprint %d,"
                " hotbar %d %d %d %d %d %d %d %d %d\n",
                b.loaded ? "" : " (options.txt not found, the defaults)", b.attack, b.use, b.forward, b.back, b.left,
                b.right, b.jump, b.sneak, b.sprint, b.hotbar[0], b.hotbar[1], b.hotbar[2], b.hotbar[3], b.hotbar[4],
                b.hotbar[5], b.hotbar[6], b.hotbar[7], b.hotbar[8]);
    std::string version;
    game::ReadVersion(proc.pid, version);
    const game::Build build = game::BuildFromVersion(version);
    std::printf("Minecraft %s, read as %s, image at 0x%llX, 0x%zX bytes\n", version.c_str(), game::BuildName(build),
                (unsigned long long)(uintptr_t)proc.base, proc.size);
    if (build == game::Build::Unknown) return 1;

    const uintptr_t base = (uintptr_t)proc.base;
    int bad = 0;
    for (size_t i = 0; i < (size_t)game::Site::Count; ++i) {
        const game::SiteDef& d = game::SiteFor(build, (game::Site)i);
        if (!d.pattern) continue;
        const std::vector<int> pat = engine::ParsePattern(d.pattern);
        std::vector<uint8_t> bytes(pat.size());
        bool ok = engine::Read(proc, base + d.rva, bytes.data(), bytes.size());
        const bool read = ok;
        for (size_t k = 0; ok && k < pat.size(); ++k) ok = pat[k] < 0 || pat[k] == bytes[k];
        bad += !ok;
        std::printf("  %-12s +0x%07X  %s\n", d.name, d.rva,
                    ok ? "ok" : !read ? "unreadable" : bytes[0] == 0xE9 ? "starts with a jump (a cave left behind?)" : "different");
    }

    const uintptr_t core = base + game::SiteFor(build, game::Site::GameCore).rva;
    int32_t disp = 0;
    if (!engine::Read(proc, core + 3, disp)) return 1;
    bedrock::Reader rd;
    rd.Attach(proc, core + 7 + (intptr_t)disp, game::LayoutFor(build));

    uintptr_t ci = 0;
    if (!rd.ClientInstance(ci)) {
        std::printf("no ClientInstance\n");
        return 1;
    }
    std::printf("ClientInstance 0x%llX\n", (unsigned long long)ci);
    uintptr_t timer = 0;
    float tps = 0, alpha = 0;
    if (rd.Timer(timer, tps, alpha))
        std::printf("Timer 0x%llX: %.2f ticks a second, %.3f of a tick drawn\n", (unsigned long long)timer, tps, alpha);
    else
        std::printf("no Timer\n");

    bedrock::Actor me;
    if (!rd.LocalPlayer(me)) {
        std::printf("no player: not in a world\n");
        return bad ? 1 : 0;
    }
    std::printf("player 0x%llX, entity %u\n", (unsigned long long)me.ptr, me.entity);
    bedrock::Motion mo;
    if (rd.ReadMotion(me.ptr, mo))
        std::printf("  eye %.3f %.3f %.3f, last %.3f %.3f %.3f, velocity %.4f %.4f %.4f\n", mo.pos[0], mo.pos[1],
                    mo.pos[2], mo.last[0], mo.last[1], mo.last[2], mo.vel[0], mo.vel[1], mo.vel[2]);
    float mn[3], mx[3];
    if (rd.ReadBox(me.ptr, mn, mx))
        std::printf("  box %.3f %.3f %.3f to %.3f %.3f %.3f (%.2f wide, %.2f high, eye %.2f above the feet)\n", mn[0],
                    mn[1], mn[2], mx[0], mx[1], mx[2], mx[0] - mn[0], mx[1] - mn[1], mo.pos[1] - mn[1]);
    float pitch = 0, yaw = 0;
    if (rd.ReadRotation(me.ptr, pitch, yaw)) std::printf("  pitch %.2f, yaw %.2f\n", pitch, yaw);
    bool ground = false, glide = false, sprint = false;
    const bool g1 = rd.OnGround(me, ground), g2 = rd.Gliding(me, glide), g3 = rd.Sprinting(me, sprint);
    std::printf("  on ground %s, gliding %s, sprinting %s\n", g1 ? (ground ? "yes" : "no") : "?",
                g2 ? (glide ? "yes" : "no") : "?", g3 ? (sprint ? "yes" : "no") : "?");
    std::string type, tag;
    if (rd.TypeId(me.ptr, type)) std::printf("  type %s\n", type.c_str());
    if (rd.NameTag(me.ptr, tag)) std::printf("  name tag \"%s\"\n", tag.c_str());
    int slot = -1;
    if (rd.SelectedSlot(me.ptr, slot)) {
        std::printf("  holding slot %d\n", slot + 1);
        for (int i = 0; i < 9; ++i) {
            bedrock::Item it;
            if (!rd.HotbarItem(me.ptr, i, it)) {
                std::printf("    %d: unreadable\n", i + 1);
                continue;
            }
            if (!it.name.empty()) std::printf("    %d: %s x%d (aux %d)\n", i + 1, it.name.c_str(), it.count, it.aux);
        }
    }
    if (live && !bad) Live(proc, build, rd, me.ptr);
    return bad ? 1 : 0;
}
