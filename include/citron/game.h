// The Minecraft builds Citron Lite knows the insides of: where each patched or hooked
// instruction lives, and the structure offsets the reader walks.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace citron::game {

enum class Build : uint8_t { Unknown, V1_26_52, V1_26_60 };

enum class Site : uint8_t {
    GameCore,     // mov [rip+disp], r15: stores the game core global
    Camera,       // addss xmm0, [r8+4]: r8 is the render camera (yaw +0, pitch +4, radians)
    ActorRender,  // the actor renderer: rsi is the actor being drawn at +0x2E
    HurtTime,     // mov ebp, [rax]: rax points at your hurt time
    Looking,      // mov eax, [rax+0x18]: rax is the HitResult under the crosshair
    Reach,        // the pick's reach clamp
    ReachWall,    // survival's wall test after it (1.26.60)
    HotbarWheel,  // the mouse wheel's step of the selected slot (add at +7)
    AirAccel,     // the store of the air acceleration (one byte before the movss)
    JumpDelay,    // jne over the jump while the jump cooldown runs (at +5)
    HitboxWidth,  // movss xmm0, [rax+0x18]: an actor's box width
    UseDelay,     // the call that starts the item use delay after an attack (at +20)
    OwnNametag,   // je that skips your own nametag (its two bytes, per build below)
    Perspective,  // Options::getPerspective (the value read is at +0x43)
    Count
};

struct SiteDef {
    const char* name;
    bool required;        // nothing works without it
    uint32_t rva;         // where the build this table was read from has it
    const char* pattern;  // what is there, wildcards as ??
};

struct Layout {
    uint8_t ownNametagSkip[2];      // the je, as the game has it
    // game core -> MinecraftGame -> client map -> ClientInstance
    uintptr_t coreMinecraftGame;
    uintptr_t gameClientMap;
    uintptr_t clientMinecraftGame;  // ClientInstance -> the MinecraftGame it belongs to
    uintptr_t clientMinecraft;      // ClientInstance -> Minecraft
    uintptr_t minecraftTimer;       // Minecraft -> the simulation's Timer
    uintptr_t clientLocalPlayer;    // ClientInstance -> WeakEntityRef of your player

    // Actor
    uintptr_t actorRegistry;        // the entt registry the actor's components live in
    uintptr_t actorEntity;          // its entity id there
    uintptr_t actorTypeId;          // std::string, "minecraft:player"
    uintptr_t actorStateVector;     // -> position, last tick's position, velocity
    uintptr_t actorAabb;            // -> min, max, width, height
    uintptr_t actorRotation;        // -> pitch, yaw in degrees
    uintptr_t actorNameTag;         // std::string, players

    // Player
    uintptr_t playerSupplies;       // -> PlayerInventory
};

// The Timer: ticks a second, ticks this frame, the fraction of a tick drawn this frame.
constexpr uintptr_t kTimerTicksPerSecond = 0x00;
constexpr uintptr_t kTimerAlpha = 0x08;

// StateVectorComponent and AABBShapeComponent.
constexpr uintptr_t kStatePosition = 0x00, kStatePrevious = 0x0C, kStateVelocity = 0x18;
constexpr uintptr_t kAabbMin = 0x00, kAabbMax = 0x0C, kAabbWidth = 0x18, kAabbHeight = 0x1C;

// HitResult: the ray's start, what it hit, where.
constexpr uintptr_t kHitStart = 0x00, kHitType = 0x18, kHitPosition = 0x2C;

// PlayerInventory and its 36 slot container; ItemStack and Item.
constexpr uintptr_t kSuppliesSelected = 0x10, kSuppliesInventory = 0xB8;
constexpr size_t kItemStackSize = 0x98, kInventorySlots = 36;
constexpr uintptr_t kStackItem = 0x08, kStackAux = 0x20, kStackCount = 0x22;
constexpr uintptr_t kItemName = 0x128;

// entt component type hashes (FNV-1a of the component's name).
constexpr uint32_t kActorOwnerComponent = 0x85B93800u;
constexpr uint32_t kOnGroundFlagComponent = 0xC29078A0u;
constexpr uint32_t kActorDataFlagComponent = 0xC67426F3u;
constexpr int kFlagSprinting = 3, kFlagGliding = 32;

Build BuildFromVersion(std::string_view version);
const char* BuildName(Build build);
const SiteDef& SiteFor(Build build, Site site);
const Layout& LayoutFor(Build build);

// The installed package's version ("1.26.60.5"), from its package identity or the exe's version
// resource. False when the process cannot be asked.
bool ReadVersion(uint32_t processId, std::string& out);

}  // namespace citron::game
