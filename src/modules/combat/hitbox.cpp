#include "../../backend/backend_impl.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace citron {
namespace {

// Cave data, after the code.
constexpr uintptr_t kWidth = 0x50, kShownWidth = 0x54, kVtable = 0x58, kHeight = 0x60;

void Emit(std::vector<uint8_t>& c, std::initializer_list<uint8_t> b) { c.insert(c.end(), b); }
void Rel32(std::vector<uint8_t>& c, int32_t v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    c.insert(c.end(), p, p + 4);
}

}  // namespace

// The site is an actor's box width getter: `movss xmm0, [rax+0x18]` with rax the AABBShapeComponent
// and rsi the actor. For other players (their class's vtable) the cave stores the widened box on the
// component, which is what the game's hit test reads, and hands the caller either that width or,
// with Shadow Spoof, the real one. The plain path comes last and ends like every other cave, with
// the stolen instruction before the jump back, so a cave left behind can be healed.
bool Backend::Impl::HitboxInstall(uintptr_t target) {
    void* cave = engine::AllocCaveNear(proc, target, 0x1000);
    if (!cave) return false;
    hitboxHook.cave = (uintptr_t)cave;
    hitboxHook.slot = hitboxHook.cave + 0x100;
    hitboxHook.match = target;
    hitboxHook.stolen = 5;
    if (!engine::Read(proc, target, hitboxHook.orig, 5)) {
        engine::RemoveHook(proc, hitboxHook);
        return false;
    }
    std::vector<uint8_t> c;
    auto disp = [&](uintptr_t field, size_t endOfInstruction) { return (int32_t)((int64_t)field - (int64_t)endOfInstruction); };
    auto back = [&] {
        Emit(c, {0xE9});                             // jmp back
        Rel32(c, (int32_t)((int64_t)(target + 5) - (int64_t)(hitboxHook.cave + c.size() + 4)));
    };

    Emit(c, {0x52, 0x41, 0x50});                     // push rdx; push r8
    Emit(c, {0x48, 0x8B, 0x16});                     // mov rdx, [rsi]
    Emit(c, {0x4C, 0x8B, 0x05});                     // mov r8, [rip + vtable]
    Rel32(c, disp(kVtable, c.size() + 4));
    Emit(c, {0x4C, 0x39, 0xC2});                     // cmp rdx, r8
    const size_t jne = c.size();
    Emit(c, {0x0F, 0x85, 0, 0, 0, 0});               // jne plain
    Emit(c, {0x8B, 0x15});                           // mov edx, [rip + width]
    Rel32(c, disp(kWidth, c.size() + 4));
    Emit(c, {0x89, 0x50, 0x18});                     // mov [rax+0x18], edx
    Emit(c, {0x8B, 0x15});                           // mov edx, [rip + height]
    Rel32(c, disp(kHeight, c.size() + 4));
    Emit(c, {0x89, 0x50, 0x1C});                     // mov [rax+0x1C], edx
    Emit(c, {0x41, 0x58, 0x5A});                     // pop r8; pop rdx
    Emit(c, {0xF3, 0x0F, 0x10, 0x05});               // movss xmm0, [rip + shown width]
    Rel32(c, disp(kShownWidth, c.size() + 4));
    back();
    const size_t plain = c.size();
    Emit(c, {0x41, 0x58, 0x5A});                     // plain: pop r8; pop rdx
    c.insert(c.end(), hitboxHook.orig, hitboxHook.orig + 5);   // movss xmm0, [rax+0x18]
    back();
    const int32_t toPlain = (int32_t)(plain - (jne + 6));
    std::memcpy(&c[jne + 2], &toPlain, 4);
    if (c.size() > kWidth) {
        engine::RemoveHook(proc, hitboxHook);
        return false;
    }

    const uintptr_t none = 0;
    const float width = 0.6f, height = 1.8f;
    if (!engine::Write(proc, hitboxHook.cave + kVtable, none) || !engine::Write(proc, hitboxHook.cave + kWidth, width) ||
        !engine::Write(proc, hitboxHook.cave + kShownWidth, width) || !engine::Write(proc, hitboxHook.cave + kHeight, height) ||
        !engine::WriteProtected(proc, hitboxHook.cave, c.data(), c.size())) {
        engine::RemoveHook(proc, hitboxHook);
        return false;
    }
    uint8_t jump[5] = {0xE9};
    const int32_t rel = (int32_t)((int64_t)hitboxHook.cave - (int64_t)(target + 5));
    std::memcpy(jump + 1, &rel, 4);
    if (!engine::WriteProtected(proc, target, jump, 5)) {
        engine::RemoveHook(proc, hitboxHook);
        return false;
    }
    hitboxHook.installed = true;
    Log("[hitbox] installed at 0x%llX", (unsigned long long)target);
    return true;
}

// The widened boxes live in the entities themselves: pulling the hook stops new writes, this puts
// back the ones already made (or every player keeps a giant shadow until the server resends it).
void Backend::Impl::RestoreHitboxSizes() {
    if (!proc.ok()) return;
    const float width = 0.6f + 2.f * hitboxWidth, height = 1.8f + hitboxHeight;
    if (width <= 0.601f && height <= 1.801f) return;
    for (const auto& kv : ents) {
        const Ent& e = kv.second;
        float wh[2] = {};
        if (!e.box || !engine::Read(proc, e.box + game::kAabbWidth, wh, sizeof wh)) continue;
        if (std::fabs(wh[0] - width) > 0.001f || std::fabs(wh[1] - height) > 0.001f) continue;
        const float vanilla[2] = {0.6f, 1.8f};
        engine::Write(proc, e.box + game::kAabbWidth, vanilla, sizeof vanilla);
    }
}

void Backend::Impl::HitboxTick(const Modules& m) {
    const ModuleSnapshot* h = Mod(m, "hitbox");
    const uintptr_t target = SiteAt(game::Site::HitboxWidth);
    if (!h || !h->on || !target) {
        if (hitboxHook.installed) {
            engine::RemoveHook(proc, hitboxHook);
            hitboxHook = engine::CaveHook{};
            RestoreHitboxSizes();
        }
        return;
    }
    const float w = std::clamp(Num(*h, "width", 0.3f), 0.f, 1.f), ht = std::clamp(Num(*h, "height", 0.3f), 0.f, 1.f);
    const bool spoof = Check(*h, "shadow", true);
    const bool fresh = !hitboxHook.installed;
    if (fresh && !HitboxInstall(target)) return;
    if (!fresh && w == hitboxWidth && ht == hitboxHeight && spoof == hitboxStealth && remotePlayerVtable == hitboxVtable)
        return;
    if (!fresh && (w != hitboxWidth || ht != hitboxHeight)) RestoreHitboxSizes();
    hitboxWidth = w;
    hitboxHeight = ht;
    hitboxStealth = spoof;
    hitboxVtable = remotePlayerVtable;
    const float width = 0.6f + 2.f * w, height = 1.8f + ht, shown = spoof ? 0.6f : width;
    engine::Write(proc, hitboxHook.cave + kWidth, width);
    engine::Write(proc, hitboxHook.cave + kShownWidth, shown);
    engine::Write(proc, hitboxHook.cave + kHeight, height);
    engine::Write(proc, hitboxHook.cave + kVtable, hitboxVtable);
}

}  // namespace citron
