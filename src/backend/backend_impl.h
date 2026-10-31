#pragma once
#include "citron/backend.h"
#include "citron/bedrock.h"
#include "citron/engine.h"
#include "citron/fastfall_core.h"
#include "citron/game.h"
#include "citron/game_bindings.h"
#include "citron/log.h"
#include "citron/model.h"
#include "citron/platform.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace citron {

using clock_t_ = std::chrono::steady_clock;
using Modules = std::map<std::string, ModuleSnapshot>;

// Input (input.cpp).
// Physical state only: everything this client sends itself is injected and never counted here.
bool PhysKeyDown(int vk);
uint32_t PhysKeyEdges(int vk);
void ResetPhysicalInput();
void SetHookBindings(const gamebind::Bindings& b);
uint32_t AttackPresses();           // physical presses of the attack binding, monotonic
void CountAttack();                 // a press this client made, so landed hits see it too

// SnapTap: newest movement key wins. 0 off, 1 both pairs, 2 forward/back, 3 left/right.
void SetSnapTap(int mode);
// While a tap holds forward up, the hook swallows its auto-repeat so the game does not press it again.
void SetForwardHeldBack(bool on);
bool ForwardPhysical();

// The zoom bind takes the wheel: notches are counted instead of reaching the hotbar.
void SetWheelOwned(bool owned, unsigned long gamePid);
int TakeWheelNotches();

// Timing (timing.cpp).
struct Xrng {
    uint64_t s[4] = {1, 2, 3, 4};
    void seed(uint64_t salt);
    uint64_t next();
    double unit();
    int range(int lo, int hi);   // [lo, hi)
    double gaussian();
};

// The interval between clicks, shaped like a hand rather than a timer.
struct HumanDelay {
    double drift = 0.0;
    struct Pair { double up, down; };
    Pair get(double minCps, double maxCps, Xrng& rng);
};

// Aim shake: a continuous path, not noise.
struct SmoothJit {
    double t = 0.0;
    void reset() { t = 0.0; }
    bool next(int intensity, Xrng& rng, int& ox, int& oy);
};

struct BtnClk {
    bool down = false, active = false;
    int count = 0;
    double curUp = 0.0;
    clock_t_::time_point next{};
};

// The world.
struct Ent {
    uintptr_t actor = 0;
    uint32_t entity = 0;
    uintptr_t vtable = 0;
    uintptr_t box = 0;               // its AABBShapeComponent
    float mn[3] = {}, mx[3] = {};    // the box as it is drawn this frame
    float pos[3] = {}, last[3] = {};
    bool player = false;
    bool lifeless = false;           // an item, arrow, boat and the like
    bool self = false;
    int team = 0;                    // colour family of the first colour code in its name tag
    std::string name;                // name tag without colour codes, first line
    clock_t_::time_point named{};
    uint32_t pass = 0;               // the last list it was on
    bool typed = false;
};

struct Local {
    bool valid = false;
    bedrock::Actor actor;
    bedrock::Motion motion;
    float eye[3] = {};               // where the camera is this frame
    int team = 0;
    clock_t_::time_point lookedUp{}, named{};
};

struct Look {
    bool valid = false;
    float yaw = 0.f, pitch = 0.f;    // radians, the camera's convention
    uintptr_t camera = 0;
};

// A small patch of game code: what was there and whether ours is in.
struct CodePatch {
    uintptr_t at = 0;
    uint8_t original[8] = {};
    int size = 0;
    bool saved = false, on = false;
};

struct Backend::Impl {
    engine::Proc proc;
    game::Build build = game::Build::Unknown;
    bedrock::Reader rd;
    std::array<uintptr_t, (size_t)game::Site::Count> site{};
    uintptr_t gameCoreSlot = 0;
    gamebind::Bindings binds{};
    clock_t_::time_point nextBindRefresh{};

    // threads and the UI's snapshot
    std::thread th, hookTh;
    std::atomic<DWORD> hookTid{0};
    std::atomic<bool> running{false};
    std::atomic<bool> guiOpen{false};   // the Click GUI: nothing acts, whatever its settings say
    std::mutex stopMtx;
    std::condition_variable stopCv;
    std::mutex hookStartMtx;
    std::condition_variable hookStartCv;
    bool hookStarted = false;
    mutable std::mutex mtx;
    Modules snap;
    std::atomic<uint64_t> snapRevision{0};
    Modules modulesUi;
    BackendStatus status;
    HWND hwnd = nullptr;

    // the world, refreshed by the worker
    Local local;
    Look look;
    std::unordered_map<uintptr_t, Ent> ents;
    float alpha = 1.f;
    uintptr_t timerAt = 0;
    float timerTps = 20.f;
    uintptr_t remotePlayerVtable = 0;
    clock_t_::time_point nextEntityPass{}, nextBoxPass{}, nextCoreTry{};
    std::vector<bedrock::ActorRef> actorList;
    uint32_t entityPass = 0;
    bool wantActors = false;

    // focus
    HWND focusWindow = nullptr;
    DWORD focusPid = 0;
    bool focusVerdict = false;
    ULONGLONG focusAt = 0;

    // caves
    engine::CaveHook camHook, hurtHook, lookHook, perspHook, hitboxHook, airHook, slotHook, cursorHook;

    // the crosshair
    bool lookBlock = false, lookEntity = false, lookKnown = false;
    float lookStart[3] = {}, lookHit[3] = {};
    clock_t_::time_point lookBlockAt{};

    // landed hits
    struct AttackSnap { bool entity = false; uintptr_t target = 0; };
    AttackSnap attackLive;
    uint32_t attackSeen = 0;
    struct LandedHit { bool fresh = false; float distance = 0.f; uintptr_t target = 0; uint32_t count = 0;
                       clock_t_::time_point at{}; } landed;
    float curReach = 3.f;

    // Reach and Ghost Hand
    uintptr_t reachFloats = 0;        // our page near the image: survival's reach, creative's
    uint8_t reachOriginal[22] = {};
    bool reachInstalled = false;
    bool reachKeep = false;           // our page stays pointed at when Reach is put back
    int reachState = 0;               // 0 vanilla, 1 Reach, 2 Ghost Hand
    float reachTarget = 3.f;
    clock_t_::time_point reachNextRoll{};
    bool reachChanceOn = true;

    // small patches
    CodePatch jumpDelay, useDelay, ownNametag, fovLimit;

    // the Click GUI: the game's mouse steps, skipped while it is open
    CodePatch turnSkip, mouseSkip;
    bool guiHeld = false, guiClosing = false;
    clock_t_::time_point guiCloseBy{}, nextGuiTry{};

    // Aim Assist
    uintptr_t aimTarget = 0;
    clock_t_::time_point aimAcquiredAt{}, aimLastStep{};
    float aimDriftYaw = 0.f, aimDriftPitch = 0.f;
    float aimLastYaw = 0.f;           // the yaw last seen or written, to measure the hand
    bool aimHaveYaw = false;
    float aimHand = 0.f, aimIntent = 0.f;   // your own turning: the last 40 ms, and a decaying sum
    clock_t_::time_point aimHandAt{}, aimIntentAt{};
    Xrng aimRng;

    // Trigger Bot
    struct Trig { bool down = false; uintptr_t target = 0; clock_t_::time_point seenAt{}, releaseAt{}, nextClick{};
                  int delayMs = 0; } trig;
    HumanDelay trigHd;
    Xrng trigRng;

    // Auto Clicker
    BtnClk leftClk, rightClk;
    Xrng leftRng, rightRng, jitRng;
    HumanDelay leftHd, rightHd;
    SmoothJit clickJit;
    clock_t_::time_point jitterNext{};

    // taps after a hit
    bool tapActive = false;
    clock_t_::time_point tapEnd{};
    uint32_t tapSeenHit = 0;
    Xrng tapRng;

    // Jump Reset
    int prevHurt = 0;
    bool hurtPrimed = false, jumpDown = false, jumpPending = false, jumpSent = false, jumpContext = false;
    clock_t_::time_point jumpUp{}, jumpFireAt{}, jumpLastHit{};
    int jumpHitN = 0;
    uintptr_t jumpOpponent = 0;
    Xrng jumpRng;

    // Velocity
    int velPrevHurt = 0;
    bool velPrimed = false;
    clock_t_::time_point velLastApplied{};
    float velKeepH = 1.f, velKeepV = 1.f;
    Xrng velRng;

    // Auto Sprint
    bool sprintHeld = false;
    int sprintCode = 0;
    bool sprintRepress = false, sprintReleased = false, sprintStalled = false;
    clock_t_::time_point sprintRepressAt{};

    // Keybind Pearl, Snowball and Pot
    struct ItemThrow {
        int stage = 0;                // 0 idle
        int slot = -1, back = -1;
        bool useDown = false;
        clock_t_::time_point step{}, abortAt{};
        std::string module;
    } throwing;
    std::map<std::string, uint32_t> throwEdges;

    // the game's clock: Timer, and Fast Fall while a fall is helped
    bool clockOurs = false;
    float clockSet = 20.f;
    clock_t_::time_point clockCheck{};
    fastfall::Core fall;
    bool fallActing = false;
    float fallFactor = 1.f;
    uint64_t fallRng = 0;
    clock_t_::time_point fallFlagsAt{};
    bool groundOk = false, onGround = false, glideOk = false, gliding = false;

    // Zoom and Custom FOV: the FOV option's value
    uintptr_t fovAddr = 0;
    int fovStaleLooks = 0;
    clock_t_::time_point fovNextLook{};
    bool zoomActive = false;
    float zoomPrevFov = 0.f, zoomCurrentFov = 0.f, zoomTargetFov = 0.f, zoomScrollAdjust = 0.f;
    clock_t_::time_point zoomLastTick{};
    bool customFovOn = false;
    float customFovOriginal = 0.f;
    bool fovBoundsRead = false;
    float fovMinGiven = 0.f, fovMaxGiven = 0.f, fovMinSet = 0.f, fovMaxSet = 0.f;   // the option record's own bounds, and what is there now
    uintptr_t fovCeiling = 0;         // our page: the float getFov's last clamp reads while it is lifted

    // Zoom's wheel: the hotbar stays put
    bool slotsHeld = false;
    clock_t_::time_point slotsFreeAt{}, nextSlotTry{};
    uint32_t hotbarSeen[9] = {};

    // Fullbright
    uintptr_t gammaRecord = 0;
    float gammaOriginal = 0.f;
    bool gammaPinned = false;
    clock_t_::time_point gammaNextEnforce{};

    // Hitbox
    float hitboxWidth = 0.f, hitboxHeight = 0.f;
    bool hitboxStealth = false;
    uintptr_t hitboxVtable = 0;

    // Air Accel
    float airWrote = -1.f;

    // Friends: middle click on a player
    uint32_t middleSeen = 0;

    // Stream Hide and Taskbar Hide
    bool lastStreamHide = false, streamHideKnown = false, streamHideRequest = false;
    unsigned streamHideRetries = 0;
    clock_t_::time_point streamHideRetryAt{};

    uint32_t rngState = 0x1234567u;
    float Rand01() {
        rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
        return (rngState & 0xFFFFFF) / float(0x1000000);
    }

    // Lifecycle (backend.cpp).
    void Start();
    void Stop();
    void Worker();
    void HookThread();
    bool WaitWhileRunning(std::chrono::milliseconds d);
    bool Attach();
    void HealGuiSkips();
    bool ScanSites();
    void RestoreAll();
    void ForgetGame();
    void SetStatus(bool attached, const char* label, std::string detail);
    void RefreshBindings(bool force = false);
    void GuiHoldTick(const Modules& m);
    uintptr_t SiteAt(game::Site s) const { return site[(size_t)s]; }

    // Shared helpers (backend.cpp).
    bool GameFocused();
    bool CursorHidden() const;
    bool InGame() { return !guiOpen.load(std::memory_order_relaxed) && GameFocused() && CursorHidden(); }
    bool PhysicalBindingDown(int code) const;
    bool WritePatch(CodePatch& p, uintptr_t at, const uint8_t* bytes, int n, const uint8_t* expect);
    void ClearPatch(CodePatch& p);
    static bool On(const Modules& m, const char* id);
    static const ModuleSnapshot* Mod(const Modules& m, const char* id);
    static float Num(const ModuleSnapshot& s, const char* key, float def);
    static bool Check(const ModuleSnapshot& s, const char* key, bool def);
    static int Pick(const ModuleSnapshot& s, const char* key, int def);
    bool ChanceAllows(const ModuleSnapshot& s);

    // The world (world.cpp).
    void RefreshLocal();
    void CoreHooksTick(const Modules& m);
    void RefreshLook();
    void RefreshEntities();
    bool RefreshEnt(Ent& e);
    void RefreshCrosshair(const Modules& m);
    void LandedHitTick(const Modules& m);
    bool Fightable(const Ent& e, const Modules& m, bool playersOnly) const;
    const Ent* UnderCrosshair(float reach, const Modules& m, bool playersOnly, float* distance = nullptr) const;
    bool AttackerInCone(float coneDeg, float range, bool requireOne, const Modules& m) const;
    void FriendClickTick(const Modules& m);
    bool HeldItem(bedrock::Item& out);
    bool HoldingWeapon();

    // Modules.
    void AimAssistTick(const Modules& m);
    void TriggerBotTick(const Modules& m);
    void ClickerTick(const Modules& m);
    void DriveButton(BtnClk& st, HumanDelay& hd, Xrng& rng, bool want, int code, float mn, float mx, bool hold);
    void SendAttack(bool down);
    void ReachTick(const Modules& m);
    bool ReachInstall();
    void ReachRestore();
    void HitboxTick(const Modules& m);
    bool HitboxInstall(uintptr_t target);
    void RestoreHitboxSizes();
    void ThrowTick(const Modules& m);
    void FinishThrow(bool goBack);
    void NoUseDelayTick(const Modules& m);
    void STapTick(const Modules& m);
    void FinishTap();
    void SnapTapTick(const Modules& m);
    void JumpResetTick(const Modules& m);
    void CancelJumpReset();
    void NoJumpDelayTick(const Modules& m);
    void VelocityTick(const Modules& m);
    void AirAccelTick(const Modules& m);
    void AutoSprintTick(const Modules& m);
    void ReleaseSprint();
    void ClockTick(const Modules& m);
    void FastFallTick(const Modules& m);
    void ReleaseClock();
    void FovTick(const Modules& m);
    void FindFov();
    bool FovRecordLooksRight(uintptr_t valueAddr, float* valueOut = nullptr) const;
    void ZoomTick(const Modules& m);
    bool ZoomStep(const ModuleSnapshot* s, clock_t_::time_point now);
    void RestoreZoom();
    void CustomFovTick(const Modules& m);
    void FovBoundsTick(const Modules& m);
    void SetFovBounds(float lo, float hi);
    void RestoreFovBounds();
    void SetFovCeiling(bool lifted);
    void SetWheelSuppressed(bool on);
    void FullbrightTick(const Modules& m);
    bool GammaRecordLooksRight(uintptr_t record, float& value, float& fallback) const;
    void RestoreGamma();
    void NametagTick(const Modules& m);
    void WindowFeaturesTick(const Modules& m);
};

}  // namespace citron
