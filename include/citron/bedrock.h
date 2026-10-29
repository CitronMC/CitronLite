// Reading Minecraft from outside: the ClientInstance, the simulation timer, your player and the
// components and items hanging off an actor. Memory reads only; nothing here calls into the game.
//
//   game core global --> ClientInstance --> Minecraft --> Timer (ticks a second, alpha)
//   ClientInstance --> WeakEntityRef of your player --> entt --> ActorOwnerComponent --> Actor
//   Actor --> StateVectorComponent (position, last position, velocity), AABBShapeComponent,
//             rotation, name tag, type id; Player --> PlayerInventory --> the 36 item stacks
//
// A walk that does not close (a menu, a loading screen) answers false and nothing is guessed.
#pragma once
#include "citron/engine.h"
#include "citron/game.h"
#include <cstdint>
#include <string>

namespace citron::bedrock {

struct Actor {
    uintptr_t ptr = 0;
    uintptr_t registry = 0;
    uint32_t entity = 0;
};

struct Motion {
    float pos[3] = {};
    float last[3] = {};
    float vel[3] = {};
    uintptr_t state = 0;   // the StateVectorComponent itself
};

struct Item {
    std::string name;      // "minecraft:ender_pearl", namespace kept
    int count = 0;
    int aux = 0;
};

class Reader {
public:
    void Attach(const engine::Proc& proc, uintptr_t gameCoreSlot, const game::Layout& layout);
    void Detach();
    bool Attached() const { return proc_ && slot_; }

    bool ClientInstance(uintptr_t& ci);
    bool Timer(uintptr_t& at, float& ticksPerSecond, float& alpha);
    bool LocalPlayer(Actor& out);

    bool ReadMotion(uintptr_t actor, Motion& out);
    bool ReadBox(uintptr_t actor, float (&mn)[3], float (&mx)[3], uintptr_t* component = nullptr);
    bool ReadRotation(uintptr_t actor, float& pitch, float& yaw);
    bool OnGround(const Actor& a, bool& on);
    bool Gliding(const Actor& a, bool& on) { return Flag(a, game::kFlagGliding, on); }
    bool Sprinting(const Actor& a, bool& on) { return Flag(a, game::kFlagSprinting, on); }
    bool Flag(const Actor& a, int bit, bool& on);
    bool TypeId(uintptr_t actor, std::string& out);
    bool NameTag(uintptr_t actor, std::string& out);

    // The hotbar slot in your hand (0..8) and what a hotbar slot holds. An empty slot reads true
    // with an empty name.
    bool SelectedSlot(uintptr_t player, int& slot);
    bool HotbarItem(uintptr_t player, int slot, Item& out);

    bool ReadString(uintptr_t at, std::string& out, size_t limit);
    bool Plausible(uintptr_t p) const { return p >= 0x10000ull && p <= 0x7FFFFFFFFFFFull; }

    template <class T> bool Rd(uintptr_t at, T& v) const { return proc_ && engine::Read(*proc_, at, &v, sizeof(T)); }
    bool Bytes(uintptr_t at, void* out, size_t n) const { return proc_ && engine::Read(*proc_, at, out, n); }

private:
    uintptr_t ResolveStorage(uintptr_t registry, uint32_t hash);
    bool PackedPosition(uintptr_t storage, uint32_t entity, uint32_t& packed);
    uintptr_t ComponentIn(uintptr_t storage, uint32_t entity, uint32_t size);
    uintptr_t WalkFromCore(uintptr_t core);
    uintptr_t FindClientInstance();
    bool StillClientInstance(uintptr_t ci);
    uintptr_t InventoryItems(uintptr_t player);
    bool InImage(uintptr_t p) const { return p >= base_ && p < end_; }

    const engine::Proc* proc_ = nullptr;
    game::Layout lay_{};
    uintptr_t base_ = 0, end_ = 0;
    uintptr_t slot_ = 0;
    uintptr_t ci_ = 0;
    uint64_t nextWalk_ = 0;
    uintptr_t childOff_ = 0;
    bool childKnown_ = false;

    // The inventory's ItemStack vector sits at an offset found once by its size.
    uintptr_t itemsOff_ = 0;
    uintptr_t itemsFor_ = 0;

    struct Known { uintptr_t registry = 0; uint32_t hash = 0; uintptr_t storage = 0; };
    Known cache_[16];
    int nextCache_ = 0;
};

}  // namespace citron::bedrock
