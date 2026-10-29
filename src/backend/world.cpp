#include "backend_impl.h"
#include "citron/names.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace citron {
namespace {

constexpr float kPi = 3.14159265f;

float WrapPi(float a) {
    a = std::fmod(a + kPi, 2.f * kPi);
    if (a < 0.f) a += 2.f * kPi;
    return a - kPi;
}

// What an actor is, from its type id: the name without the namespace or what follows it. A player's
// id grows the skin's geometry once one is applied ("minecraft:player.0.persona-..."), so the id
// itself is never compared whole.
std::string Kind(const std::string& type) {
    const std::string id = type.rfind("minecraft:", 0) == 0 ? type.substr(10) : type;
    return id.substr(0, id.find('.'));
}

// Things with a box that are not something to fight.
bool Lifeless(const std::string& id) {
    static const char* const kNames[] = {
        "item", "xp_orb", "arrow", "snowball", "egg", "ender_pearl", "splash_potion", "lingering_potion",
        "xp_bottle", "thrown_trident", "fireball", "small_fireball", "dragon_fireball", "wither_skull",
        "wither_skull_dangerous", "shulker_bullet", "llama_spit", "wind_charge_projectile",
        "breeze_wind_charge_projectile", "fishing_hook", "firework_rocket", "falling_block", "tnt",
        "minecart", "chest_minecart", "hopper_minecart", "tnt_minecart", "command_block_minecart", "boat",
        "chest_boat", "armor_stand", "painting", "leash_knot", "lightning_bolt", "area_effect_cloud",
        "evocation_fang", "ender_crystal", "eye_of_ender_signal", "npc", "agent", "tripod_camera"};
    for (const char* n : kNames)
        if (id == n) return true;
    return false;
}

// How far the drawn frame trails the tick that was last simulated.
float Back(float now, float last, float alpha) {
    const float d = now - last;
    return std::fabs(d) < 4.f ? d * (alpha - 1.f) : 0.f;
}

}  // namespace

void Backend::Impl::RefreshLocal() {
    const auto now = clock_t_::now();
    if (!rd.Attached()) {
        local = Local{};
        return;
    }
    if (!local.valid || now - local.lookedUp > std::chrono::milliseconds(250)) {
        bedrock::Actor a;
        const bool found = rd.LocalPlayer(a);
        if (!found) {
            local = Local{};
            local.lookedUp = now;
            return;
        }
        if (a.ptr != local.actor.ptr) Log("[world] your player is actor 0x%llX", (unsigned long long)a.ptr);
        local.actor = a;
        local.lookedUp = now;
    }
    if (!rd.ReadMotion(local.actor.ptr, local.motion)) {
        local.valid = false;
        local.lookedUp = {};
        return;
    }
    local.valid = true;
    float tps = 20.f, a = 1.f;
    uintptr_t at = 0;
    if (rd.Timer(at, tps, a)) {
        timerAt = at;
        timerTps = tps;
        alpha = a;
    }
    for (int k = 0; k < 3; ++k) local.eye[k] = local.motion.pos[k] + Back(local.motion.pos[k], local.motion.last[k], alpha);
    if (now - local.named > std::chrono::seconds(1)) {
        std::string tag;
        if (rd.NameTag(local.actor.ptr, tag)) local.team = names::TeamOf(tag);
        local.named = now;
    }
}

// The camera and the ring of drawn actors go in the first time a module needs them and stay until the
// game is let go: pulling the ring while Hitbox still has boxes to put back would leave them wide.
void Backend::Impl::CoreHooksTick(const Modules& m) {
    const bool view = On(m, "aimassist") || On(m, "triggerbot") || On(m, "jumpreset") || On(m, "stap") ||
                      On(m, "autosprint") || On(m, "friends");
    const bool actors = view || On(m, "hitbox");
    if ((!view || camHook.installed) && (!actors || renderHook.installed)) return;
    const auto now = clock_t_::now();
    if (now < nextCoreTry) return;
    bool failed = false;
    if (view && !camHook.installed) {
        const uintptr_t at = SiteAt(game::Site::Camera);
        failed |= !at || !engine::InstallCaptureR8Hook(proc, camHook, at, 6);
    }
    if (actors && !renderHook.installed) {
        const uintptr_t at = SiteAt(game::Site::ActorRender);
        failed |= !at || !engine::InstallRingHook(proc, renderHook, at + 0x2E, 6, 0x30, 1024);
    }
    if (failed) nextCoreTry = now + std::chrono::seconds(2);
}

void Backend::Impl::RefreshLook() {
    look = Look{};
    uintptr_t cam = 0;
    float yp[2] = {};
    if (!camHook.installed || !engine::Read(proc, camHook.slot, cam) || cam < 0x10000ull ||
        !engine::Read(proc, cam, yp, sizeof yp) || !std::isfinite(yp[0]) || !std::isfinite(yp[1]))
        return;
    look.valid = true;
    look.camera = cam;
    look.yaw = yp[0];
    look.pitch = yp[1];
}

void Backend::Impl::RefreshEntities(const Modules& m) {
    const auto now = clock_t_::now();
    if (!renderHook.installed) {
        ents.clear();
        return;
    }
    if (now >= nextEntityPass) {
        nextEntityPass = now + std::chrono::milliseconds(33);
        const int n = renderHook.ringN;
        static uintptr_t ring[1024];
        if (n > 0 && n <= 1024 && engine::Read(proc, renderHook.slot + 8, ring, sizeof(uintptr_t) * n)) {
            const uintptr_t base = (uintptr_t)proc.base, top = base + proc.size;
            for (int i = 0; i < n; ++i) {
                const uintptr_t actor = ring[i];
                if (!rd.Plausible(actor)) continue;
                auto it = ents.find(actor);
                if (it != ents.end()) {
                    it->second.seen = now;
                    continue;
                }
                uintptr_t vt = 0;
                if (!engine::Read(proc, actor, vt) || vt < base || vt >= top) continue;
                Ent e;
                e.actor = actor;
                e.vtable = vt;
                e.seen = now;
                ents.emplace(actor, e);
            }
        }
        for (auto it = ents.begin(); it != ents.end();)
            it = now - it->second.seen > std::chrono::milliseconds(400) ? ents.erase(it) : std::next(it);

        // The class other players are built from: the vtable most of them carry.
        std::map<uintptr_t, int> votes;
        for (auto& kv : ents) {
            Ent& e = kv.second;
            if (!e.typed) {
                std::string type;
                if (rd.TypeId(e.actor, type)) {
                    const std::string kind = Kind(type);
                    e.player = kind == "player";
                    e.lifeless = Lifeless(kind);
                    e.typed = true;
                }
            }
            e.self = local.valid && e.actor == local.actor.ptr;
            if (e.player && !e.self) ++votes[e.vtable];
            if (e.player && (e.named == clock_t_::time_point{} || now - e.named > std::chrono::seconds(1))) {
                std::string tag;
                if (rd.NameTag(e.actor, tag)) {
                    e.team = names::TeamOf(tag);
                    e.name = names::Plain(tag);
                }
                e.named = now;
            }
        }
        uintptr_t best = 0;
        int bestN = 0;
        for (auto& kv : votes)
            if (kv.second > bestN) best = kv.first, bestN = kv.second;
        if (best && best != remotePlayerVtable) {
            remotePlayerVtable = best;
            Log("[world] other players are built from vtable +0x%llX", (unsigned long long)(best - (uintptr_t)proc.base));
        }
    }
    (void)m;
    if (now < nextBoxPass) return;
    nextBoxPass = now + std::chrono::milliseconds(8);
    for (auto it = ents.begin(); it != ents.end();) it = RefreshEnt(it->second) ? std::next(it) : ents.erase(it);
}

// The box as it is drawn this frame: the last tick's box moved back by however far the frame trails it.
bool Backend::Impl::RefreshEnt(Ent& e) {
    uintptr_t comp = 0;
    float mn[3], mx[3];
    bedrock::Motion mo;
    if (!rd.ReadBox(e.actor, mn, mx, &comp)) return false;
    e.box = comp;
    float shift[3] = {};
    if (rd.ReadMotion(e.actor, mo)) {
        std::memcpy(e.pos, mo.pos, sizeof e.pos);
        std::memcpy(e.last, mo.last, sizeof e.last);
        for (int k = 0; k < 3; ++k) shift[k] = Back(mo.pos[k], mo.last[k], alpha);
    }
    for (int k = 0; k < 3; ++k) {
        e.mn[k] = mn[k] + shift[k];
        e.mx[k] = mx[k] + shift[k];
    }
    return true;
}

bool Backend::Impl::Fightable(const Ent& e, const Modules& m, bool playersOnly) const {
    if (e.self) return false;
    const float h = e.mx[1] - e.mn[1];
    if (h < 0.2f || h > 4.f) return false;
    if (!e.player) return !playersOnly && e.typed && !e.lifeless && h >= 0.3f;
    if (const ModuleSnapshot* teams = Mod(m, "teams"); teams && teams->on) {
        const int set = Pick(*teams, "color", 0);
        const int mine = set > 0 ? set : local.team;
        if (mine && e.team && mine == e.team) return false;
    }
    if (const ModuleSnapshot* friends = Mod(m, "friends"); friends && friends->on)
        if (names::OnList(friends->names, e.name)) return false;
    return true;
}

// The first fightable box the look ray from your eye meets within `reach`, with the game's small
// margin for picking.
const Ent* Backend::Impl::UnderCrosshair(float reach, const Modules& m, bool playersOnly, float* distance) const {
    if (!local.valid || !look.valid) return nullptr;
    const float cp = std::cos(look.pitch), sp = std::sin(look.pitch);
    const float d[3] = {-std::sin(look.yaw) * cp, sp, -std::cos(look.yaw) * cp};
    const Ent* best = nullptr;
    float bestT = reach;
    for (const auto& kv : ents) {
        const Ent& e = kv.second;
        if (!Fightable(e, m, playersOnly)) continue;
        float t0 = 0.f, t1 = bestT;
        bool through = true;
        for (int k = 0; k < 3 && through; ++k) {
            const float lo = e.mn[k] - 0.1f, hi = e.mx[k] + 0.1f;
            if (std::fabs(d[k]) < 1e-6f) {
                through = local.eye[k] >= lo && local.eye[k] <= hi;
                continue;
            }
            float a = (lo - local.eye[k]) / d[k], b = (hi - local.eye[k]) / d[k];
            if (a > b) std::swap(a, b);
            t0 = std::max(t0, a);
            t1 = std::min(t1, b);
            through = t0 <= t1;
        }
        if (through && t0 < bestT) {
            bestT = t0;
            best = &e;
        }
    }
    if (best && distance) *distance = bestT;
    return best;
}

// Whether a fightable player stands within `range` and `coneDeg` of where you look. With nobody
// around the answer is `!requireOne`.
bool Backend::Impl::AttackerInCone(float coneDeg, float range, bool requireOne, const Modules& m) const {
    if (!local.valid || !look.valid) return !requireOne;
    bool any = false;
    const float half = coneDeg * 0.5f * kPi / 180.f;
    for (const auto& kv : ents) {
        const Ent& e = kv.second;
        if (!Fightable(e, m, true)) continue;
        any = true;
        const float cx = (e.mn[0] + e.mx[0]) * 0.5f, cz = (e.mn[2] + e.mx[2]) * 0.5f;
        const float dx = local.eye[0] - cx, dz = local.eye[2] - cz;
        const float d2 = dx * dx + dz * dz;
        if (d2 < 0.0625f || d2 > range * range) continue;
        if (coneDeg >= 359.5f) return true;
        if (std::fabs(WrapPi(look.yaw - std::atan2(dx, dz))) <= half) return true;
    }
    return any ? false : !requireOne;
}

void Backend::Impl::RefreshCrosshair(const Modules& m) {
    const bool wanted = On(m, "clicker") || On(m, "triggerbot") || On(m, "stap") || On(m, "jumpreset") ||
                        On(m, "autosprint");
    const uintptr_t at = SiteAt(game::Site::Looking);
    if (wanted && !lookHook.installed && at)
        engine::InstallCaptureRaxHook(proc, lookHook, at, 6);
    else if (!wanted && lookHook.installed) {
        engine::RemoveHook(proc, lookHook);
        lookHook = engine::CaveHook{};
    }
    lookKnown = lookBlock = lookEntity = false;
    if (lookHook.installed) {
        uintptr_t hit = 0;
        uint32_t type = 0;
        if (engine::Read(proc, lookHook.slot, hit) && rd.Plausible(hit) && engine::Read(proc, hit + game::kHitType, type)) {
            lookKnown = true;
            lookBlock = (type & 0xF) == 0;
            lookEntity = type == 1;
            engine::Read(proc, hit + game::kHitStart, lookStart, sizeof lookStart);
            engine::Read(proc, hit + game::kHitPosition, lookHit, sizeof lookHit);
            if (lookBlock) lookBlockAt = clock_t_::now();
        }
    }
    // What an attack made now would hit, kept for when the press is counted. The game's own pick
    // says whether it is an entity at all; the ray test says which one.
    attackLive = AttackSnap{};
    if (lookKnown && !lookEntity) return;
    if (const Ent* e = UnderCrosshair(curReach + 0.05f, m, true)) {
        attackLive.entity = true;
        attackLive.target = e->actor;
    }
}

// A press of the attack binding with a fightable player under the crosshair within reach.
void Backend::Impl::LandedHitTick(const Modules& m) {
    landed.fresh = false;
    const uint32_t presses = AttackPresses();
    if (presses == attackSeen) return;
    attackSeen = presses;
    if (!InGame() || !attackLive.entity || !attackLive.target) return;
    auto it = ents.find(attackLive.target);
    if (it == ents.end() || !local.valid) return;
    const Ent& e = it->second;
    const float cx = (e.mn[0] + e.mx[0]) * 0.5f, cz = (e.mn[2] + e.mx[2]) * 0.5f;
    const float dx = cx - local.eye[0], dz = cz - local.eye[2];
    landed.fresh = true;
    landed.distance = std::sqrt(dx * dx + dz * dz);
    landed.target = e.actor;
    landed.at = clock_t_::now();
    ++landed.count;
    (void)m;
}

// Middle click on a player with Friends on: the backend cannot touch the UI's list itself, so the
// edit is queued for the window to apply.
void Backend::Impl::FriendClickTick(const Modules& m) {
    const uint32_t presses = PhysKeyEdges(VK_MBUTTON);
    if (presses == middleSeen) return;
    middleSeen = presses;
    const ModuleSnapshot* friends = Mod(m, "friends");
    if (!friends || !friends->on || !Check(*friends, "middleClick", true) || !InGame()) return;
    Modules none;
    if (const Ent* e = UnderCrosshair(64.f, none, true)) {
        std::string tag;
        if (!rd.NameTag(e->actor, tag)) return;
        const std::string name = names::FriendName(tag);
        if (name.empty()) return;
        std::lock_guard<std::mutex> g(mtx);
        status.friendEdits.push_back(name);
    }
}

bool Backend::Impl::HeldItem(bedrock::Item& out) {
    int slot = -1;
    return local.valid && rd.SelectedSlot(local.actor.ptr, slot) && rd.HotbarItem(local.actor.ptr, slot, out);
}

bool Backend::Impl::HoldingWeapon() {
    bedrock::Item item;
    if (!HeldItem(item)) return true;   // cannot tell: do not hold the module back
    const std::string& n = item.name;
    for (const char* w : {"sword", "_axe", "mace", "trident", "spear"})
        if (n.find(w) != std::string::npos) return true;
    return false;
}

}  // namespace citron
