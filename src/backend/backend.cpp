#include "backend_impl.h"
#include "citron/pe_signature_scan.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace citron {
namespace {

using game::Site;

// Where in each site the hook or patch sits, so a cave left behind by a run that never got to clean
// up can be found and undone before the site is verified.
int HookOffset(Site s) {
    switch (s) {
    case Site::Camera:
    case Site::HurtTime:
    case Site::Looking:
    case Site::SelectSlot:
    case Site::HitboxWidth: return 0;
    case Site::Perspective: return 0x43;
    case Site::AirAccel: return 1;
    default: return -1;
    }
}

// The modules each optional site serves, for the status line when a build lacks one.
const char* ServedBy(Site s) {
    switch (s) {
    case Site::HurtTime: return "Jump Reset, Velocity";
    case Site::Looking: return "Trigger Bot";
    case Site::Reach: return "Reach, Ghost Hand";
    case Site::SelectSlot: return "Zoom's scroll";
    case Site::FovLimit: return "Custom FOV past 130";
    case Site::AirAccel: return "Air Accel";
    case Site::JumpDelay: return "No Jump Delay";
    case Site::HitboxWidth: return "Hitbox";
    case Site::UseDelay: return "No Use Delay";
    case Site::OwnNametag: return "Third Person Nametags";
    default: return nullptr;
    }
}

HANDLE g_stopEvent = nullptr;
HANDLE g_hrTimer = nullptr;

}  // namespace

// Helpers.
bool Backend::Impl::On(const Modules& m, const char* id) {
    const auto it = m.find(id);
    return it != m.end() && it->second.on;
}

const ModuleSnapshot* Backend::Impl::Mod(const Modules& m, const char* id) {
    const auto it = m.find(id);
    return it == m.end() ? nullptr : &it->second;
}

float Backend::Impl::Num(const ModuleSnapshot& s, const char* key, float def) {
    const auto it = s.sliders.find(key);
    return it == s.sliders.end() ? def : it->second;
}

bool Backend::Impl::Check(const ModuleSnapshot& s, const char* key, bool def) {
    const auto it = s.checks.find(key);
    return it == s.checks.end() ? def : it->second;
}

int Backend::Impl::Pick(const ModuleSnapshot& s, const char* key, int def) {
    const auto it = s.choices.find(key);
    return it == s.choices.end() ? def : it->second;
}

bool Backend::Impl::ChanceAllows(const ModuleSnapshot& s) { return Rand01() * 100.f <= Num(s, "chance", 100.f); }

bool Backend::Impl::GameFocused() {
    const HWND fg = GetForegroundWindow();
    const ULONGLONG now = GetTickCount64();
    if (fg && fg == focusWindow && proc.pid == focusPid && focusAt && now - focusAt < 1000) return focusVerdict;
    focusWindow = fg;
    focusPid = proc.pid;
    focusAt = now;
    focusVerdict = proc.pid && platform::ForegroundIsBedrock(proc.pid);
    return focusVerdict;
}

// In gameplay the game hides the cursor; any screen (chat, inventory, pause) shows it.
bool Backend::Impl::CursorHidden() const {
    CURSORINFO ci{};
    ci.cbSize = sizeof ci;
    return GetCursorInfo(&ci) && !(ci.flags & CURSOR_SHOWING);
}

bool Backend::Impl::PhysicalBindingDown(int code) const {
    switch (code) {
    case -99: return PhysKeyDown(VK_LBUTTON);
    case -98: return PhysKeyDown(VK_RBUTTON);
    case -97: return PhysKeyDown(VK_MBUTTON);
    case -95: return PhysKeyDown(VK_XBUTTON1);
    case -94: return PhysKeyDown(VK_XBUTTON2);
    default: return code > 0 && PhysKeyDown(code);
    }
}

// Writes `bytes` at `at`, the first time remembering what was there. `expect`, when given, is what
// must be there before anything is written: the original or (a run that never cleaned up) ours.
bool Backend::Impl::WritePatch(CodePatch& p, uintptr_t at, const uint8_t* bytes, int n, const uint8_t* expect) {
    if (!at || n <= 0 || n > 8) return false;
    if (!p.saved || p.at != at) {
        uint8_t now[8] = {};
        if (!engine::Read(proc, at, now, n)) return false;
        if (expect && std::memcmp(now, expect, n) != 0 && std::memcmp(now, bytes, n) != 0) return false;
        p.at = at;
        p.size = n;
        std::memcpy(p.original, expect ? expect : now, n);
        p.saved = true;
    }
    if (!engine::WriteProtected(proc, at, bytes, n)) return false;
    p.on = std::memcmp(bytes, p.original, n) != 0;
    return true;
}

void Backend::Impl::ClearPatch(CodePatch& p) {
    if (p.saved && p.on && proc.ok()) engine::WriteProtected(proc, p.at, p.original, p.size);
    p.on = false;
}

void Backend::Impl::RefreshBindings(bool force) {
    const auto now = clock_t_::now();
    if (!force && now < nextBindRefresh) return;
    nextBindRefresh = now + std::chrono::seconds(2);
    binds = gamebind::Resolve(proc.h);
    SetHookBindings(binds);
}

void Backend::Impl::SetStatus(bool attached, const char* label, std::string detail) {
    std::lock_guard<std::mutex> g(mtx);
    (void)attached;
    if (!std::strcmp(label, "Searching")) status.kind = BackendStatus::Kind::Searching;
    else if (!std::strcmp(label, "Unsupported")) status.kind = BackendStatus::Kind::Unsupported;
    else if (!std::strcmp(label, "Partial")) status.kind = BackendStatus::Kind::Partial;
    else status.kind = BackendStatus::Kind::Attached;
    status.detail = std::move(detail);
}

// Attaching.
bool Backend::Impl::Attach() {
    if (!engine::Attach(proc, "Minecraft.Windows.exe")) return false;
    std::string version;
    game::ReadVersion(proc.pid, version);
    build = game::BuildFromVersion(version);
    {
        std::lock_guard<std::mutex> g(mtx);
        status.version = version;
    }
    Log("[attach] Minecraft %s (pid %lu), read as %s", version.empty() ? "of unknown version" : version.c_str(),
        (unsigned long)proc.pid, game::BuildName(build));
    RefreshBindings(true);
    if (!WaitWhileRunning(std::chrono::milliseconds(400))) return false;
    if (!ScanSites()) {
        SetStatus(true, "Unsupported", "This version of Minecraft is not supported yet");
        Log("[attach] no known build matches this game");
        return true;
    }
    int32_t disp = 0;
    const uintptr_t core = SiteAt(Site::GameCore);
    if (engine::Read(proc, core + 3, disp)) gameCoreSlot = core + 7 + (intptr_t)disp;
    rd.Attach(proc, gameCoreSlot, game::LayoutFor(build));

    std::vector<std::string> missing;
    std::string detail;
    for (size_t i = 0; i < site.size(); ++i) {
        const Site s = (Site)i;
        if (site[i] || !game::SiteFor(build, s).pattern) continue;
        if (const char* who = ServedBy(s)) missing.push_back(who);
    }
    for (const std::string& m : missing) detail += (detail.empty() ? "" : ", ") + m;
    {
        std::lock_guard<std::mutex> g(mtx);
        status.missing = missing;
    }
    SetStatus(true, missing.empty() ? "Attached" : "Partial",
              missing.empty() ? std::string() : "Not on this build: " + detail);
    Log("[attach] build %s, game core global at image+0x%llX%s%s", game::BuildName(build),
        (unsigned long long)(gameCoreSlot - (uintptr_t)proc.base), missing.empty() ? "" : "; missing: ", detail.c_str());
    return true;
}

// The build the version names is tried first. When all its sites sit where that build has them,
// that is the game. Otherwise both are tried and the one with the most sites at home wins, then the
// most found anywhere: the required sites read the same on both, so finding them proves nothing.
// An unknown hotfix of a known build lands here too.
bool Backend::Impl::ScanSites() {
    site.fill(0);
    const uintptr_t base = (uintptr_t)proc.base;
    std::vector<uint8_t> image;
    bool imageRead = false;
    const game::Build order[2] = {build == game::Build::V1_26_52 ? game::Build::V1_26_52 : game::Build::V1_26_60,
                                  build == game::Build::V1_26_52 ? game::Build::V1_26_60 : game::Build::V1_26_52};
    std::array<uintptr_t, (size_t)Site::Count> best{};
    game::Build bestBuild = game::Build::Unknown;
    int bestHome = -1, bestTotal = -1;
    for (const game::Build b : order) {
        std::array<uintptr_t, (size_t)Site::Count> found{};
        bool ok = true;
        int home = 0, total = 0, patterned = 0;
        for (size_t i = 0; i < found.size() && ok; ++i) {
            const game::SiteDef& def = game::SiteFor(b, (Site)i);
            if (!def.pattern) continue;
            ++patterned;
            const std::vector<int> pat = engine::ParsePattern(def.pattern);
            auto verify = [&](uintptr_t at) {
                std::vector<uint8_t> bytes(pat.size());
                if (!engine::Read(proc, at, bytes.data(), bytes.size())) return false;
                for (size_t k = 0; k < pat.size(); ++k)
                    if (pat[k] >= 0 && (uint8_t)pat[k] != bytes[k]) return false;
                return true;
            };
            if (def.rva) {
                const uintptr_t at = base + def.rva;
                const int hook = HookOffset((Site)i);
                if (verify(at) || (hook >= 0 && engine::HealStaleCave(proc, at + hook) > 0 && verify(at))) {
                    found[i] = at;
                    ++home;
                    ++total;
                    continue;
                }
            }
            if (!imageRead) {
                imageRead = true;
                if (!engine::ReadModule(proc, image)) image.clear();
            }
            if (!image.empty()) {
                const auto hits = sigscan::FindExecutableMatches(image.data(), image.size(), base, pat, 2);
                if (hits.exact(1)) {
                    found[i] = hits.addresses.front();
                    ++total;
                }
            }
            if (!found[i] && def.required) ok = false;
        }
        if (!ok) continue;
        if (home > bestHome || (home == bestHome && total > bestTotal)) {
            best = found;
            bestBuild = b;
            bestHome = home;
            bestTotal = total;
        }
        if (home == patterned) break;
    }
    if (bestBuild == game::Build::Unknown) return false;
    site = best;
    if (build != bestBuild) Log("[attach] the game's code reads as %s", game::BuildName(bestBuild));
    build = bestBuild;
    return true;
}

// The game went away: everything it held went with it, so nothing is restored, only forgotten.
void Backend::Impl::ForgetGame() {
    if (trig.down) SendAttack(false);
    trig = Trig{};
    FinishTap();
    CancelJumpReset();
    ReleaseSprint();
    SetSnapTap(0);
    SetWheelOwned(false, 0);
    rd.Detach();
    camHook = hurtHook = lookHook = perspHook = hitboxHook = airHook = slotHook = engine::CaveHook{};
    jumpDelay = useDelay = ownNametag = fovLimit = CodePatch{};
    reachInstalled = reachKeep = false;
    reachFloats = 0;
    reachState = 0;
    clockOurs = false;
    fall.Reset();
    fallActing = false;
    fovAddr = 0;
    fovScanCursor = 0;
    fovScanDone = false;
    fovBoundsRead = false;
    fovCeiling = 0;
    zoomActive = false;
    customFovOn = false;
    slotsHeld = false;
    gammaRecords.clear();
    gammaCandidates.clear();
    gammaOriginal.clear();
    gammaScan = GammaScan{};
    gammaPinned = gammaWasOn = false;
    gammaRetries = 0;
    ents.clear();
    local = Local{};
    look = Look{};
    remotePlayerVtable = 0;
    site.fill(0);
    gameCoreSlot = 0;
    nextCoreTry = {};
    build = game::Build::Unknown;
    engine::ResetCaveCache();
    engine::Detach(proc);
}

// Everything this client put into the game goes back, in the order that cannot leave a half state.
void Backend::Impl::RestoreAll() {
    if (trig.down) SendAttack(false);
    trig = Trig{};
    FinishTap();
    CancelJumpReset();
    ReleaseSprint();
    SetSnapTap(0);
    SetWheelOwned(false, 0);
    FinishThrow(false);
    if (!proc.ok()) return;
    RestoreZoom();
    if (customFovOn && fovAddr && customFovOriginal > 10.f) engine::Write(proc, fovAddr, customFovOriginal);
    customFovOn = false;
    RestoreFovBounds();
    ReleaseClock();
    RestoreGamma();
    ReachRestore();
    for (CodePatch* p : {&jumpDelay, &useDelay, &ownNametag, &fovLimit}) ClearPatch(*p);
    for (engine::CaveHook* h : {&camHook, &hurtHook, &lookHook, &perspHook, &airHook, &slotHook}) {
        if (h->installed) engine::RemoveHook(proc, *h);
        *h = engine::CaveHook{};
    }
    if (hitboxHook.installed) {
        engine::RemoveHook(proc, hitboxHook);
        hitboxHook = engine::CaveHook{};
        RestoreHitboxSizes();
    }
    Log("[restore] the game is as it was");
}

// The worker.
// A wait that can be as short as a millisecond: a plain wait_for rounds up to the system clock tick,
// 15.6 ms unless some program asked for better, which turns every module's step into a slideshow.
bool Backend::Impl::WaitWhileRunning(std::chrono::milliseconds d) {
    if (g_hrTimer && g_stopEvent && d < std::chrono::milliseconds(15)) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)std::chrono::duration_cast<std::chrono::microseconds>(d).count() * 10;
        if (SetWaitableTimer(g_hrTimer, &due, 0, nullptr, nullptr, FALSE)) {
            const HANDLE both[2] = {g_stopEvent, g_hrTimer};
            const DWORD r = WaitForMultipleObjects(2, both, FALSE, INFINITE);
            if (r == WAIT_OBJECT_0) return false;
            if (r == WAIT_OBJECT_0 + 1) return running.load(std::memory_order_acquire);
        }
    }
    std::unique_lock<std::mutex> lock(stopMtx);
    return !stopCv.wait_for(lock, d, [this] { return !running.load(std::memory_order_acquire); });
}

void Backend::Impl::Worker() {
    Modules m;
    uint64_t seen = UINT64_MAX;
    int aliveCheck = 0;
    clock_t_::time_point nextAttach{};
    aimRng.seed(GetTickCount64() ^ 0xA13E5D);
    trigRng.seed(GetTickCount64() ^ 0x7816B07);
    leftRng.seed(GetTickCount64() ^ 0xA17);
    rightRng.seed(GetTickCount64() ^ 0xB29);
    jitRng.seed(GetTickCount64() ^ 0xC17);
    tapRng.seed(GetTickCount64() ^ 0x77A9C);
    jumpRng.seed(GetTickCount64() ^ 0x5EED1);
    velRng.seed(GetTickCount64() ^ 0xF00D);
    fallRng = (uint64_t)GetTickCount64() * 0x9E3779B97F4A7C15ull | 1u;

    while (running.load()) {
        const uint64_t rev = snapRevision.load(std::memory_order_acquire);
        if (rev != seen) {
            std::lock_guard<std::mutex> g(mtx);
            m = snap;
            seen = rev;
        }
        bool lost = !proc.ok();
        if (!lost && ++aliveCheck >= 200) {
            aliveCheck = 0;
            lost = !engine::Alive(proc);
        }
        if (lost) {
            if (proc.ok()) Log("[worker] Minecraft closed");
            ForgetGame();
            const auto now = clock_t_::now();
            if (now < nextAttach) {
                if (!WaitWhileRunning(std::chrono::milliseconds(50))) break;
                continue;
            }
            nextAttach = now + std::chrono::seconds(1);
            SetStatus(false, "Searching", "");
            {
                std::lock_guard<std::mutex> g(mtx);
                status.version.clear();
                status.missing.clear();
                status.inWorld = false;
            }
            if (!Attach()) {
                engine::Detach(proc);
                continue;
            }
            if (!rd.Attached()) {
                // a game we cannot read: wait for it to close, look again then
                while (running.load() && engine::Alive(proc))
                    if (!WaitWhileRunning(std::chrono::milliseconds(500))) break;
                ForgetGame();
                continue;
            }
        }

        RefreshBindings();
        RefreshLocal();
        CoreHooksTick(m);
        RefreshLook();
        RefreshEntities();
        RefreshCrosshair(m);
        LandedHitTick(m);
        FriendClickTick(m);
        {
            std::lock_guard<std::mutex> g(mtx);
            status.inWorld = local.valid;
        }

        ReachTick(m);
        HitboxTick(m);
        AimAssistTick(m);
        TriggerBotTick(m);
        ClickerTick(m);
        ThrowTick(m);
        NoUseDelayTick(m);
        STapTick(m);
        SnapTapTick(m);
        JumpResetTick(m);
        NoJumpDelayTick(m);
        VelocityTick(m);
        AirAccelTick(m);
        AutoSprintTick(m);
        FastFallTick(m);
        ClockTick(m);
        FovTick(m);
        ZoomTick(m);
        CustomFovTick(m);
        FovBoundsTick(m);
        FullbrightTick(m);
        NametagTick(m);

        if (!WaitWhileRunning(std::chrono::milliseconds(2))) break;
    }
}

void Backend::Impl::Start() {
    if (running.exchange(true)) return;
    if (!g_stopEvent) g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_hrTimer)
        g_hrTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (g_stopEvent) ResetEvent(g_stopEvent);
    ResetPhysicalInput();
    {
        std::lock_guard<std::mutex> lock(hookStartMtx);
        hookStarted = false;
    }
    hookTh = std::thread([this] { HookThread(); });
    {
        std::unique_lock<std::mutex> lock(hookStartMtx);
        hookStartCv.wait(lock, [this] { return hookStarted; });
    }
    th = std::thread([this] { Worker(); });
}

void Backend::Impl::Stop() {
    if (!running.exchange(false)) return;
    if (g_stopEvent) SetEvent(g_stopEvent);
    stopCv.notify_all();
    if (th.joinable()) th.join();
    if (const DWORD tid = hookTid.load()) PostThreadMessageW(tid, WM_QUIT, 0, 0);
    if (hookTh.joinable()) hookTh.join();
    ResetPhysicalInput();
    RestoreAll();
    engine::Detach(proc);
}

// The public side.
Backend::Backend() : impl(std::make_unique<Impl>()) {}
Backend::~Backend() { impl->Stop(); }

void Backend::Init() { impl->Start(); }
void Backend::Shutdown() { impl->Stop(); }

void Backend::SetHwnd(void* hwnd) {
    const HWND next = (HWND)hwnd;
    if (impl->hwnd == next) return;
    impl->hwnd = next;
    impl->streamHideKnown = false;
    impl->streamHideRetries = 0;
    impl->streamHideRetryAt = {};
}

void Backend::Sync(const AppState& state) {
    Modules m;
    int enabled = 0;
    for (const auto& def : state.defs) {
        const ModuleState* st = state.St(def.key);
        if (!st) continue;
        ModuleSnapshot s;
        s.on = st->on;
        s.key = st->key;
        s.bind = LabelToVK(st->key);
        s.sliders = st->sliders;
        s.checks = st->checks;
        s.choices = st->choices;
        s.names = st->names;
        if (def.hasToggle && st->on && !def.hidden) ++enabled;
        m[def.key] = std::move(s);
    }
    {
        std::lock_guard<std::mutex> g(impl->mtx);
        if (m != impl->snap) {
            impl->snap = m;
            impl->snapRevision.fetch_add(1, std::memory_order_release);
        }
        impl->status.enabledModules = enabled;
    }
    impl->modulesUi = m;
    impl->WindowFeaturesTick(m);
}

bool Backend::PressKey(int vk) {
    if (!impl->proc.ok()) return false;
    return gamebind::Send(vk, true) && gamebind::Send(vk, false);
}

void Backend::SetGuiOpen(bool open) { impl->guiOpen.store(open, std::memory_order_relaxed); }

bool Backend::PressChat() { return PressKey(gamebind::Resolve(impl->proc.h).chat); }

unsigned long Backend::GamePid() const { return impl->proc.pid; }

BackendStatus Backend::Status() const {
    std::lock_guard<std::mutex> g(impl->mtx);
    BackendStatus s = impl->status;
    s.friendEdits.clear();
    return s;
}

std::vector<std::string> Backend::TakeFriendEdits() {
    std::lock_guard<std::mutex> g(impl->mtx);
    std::vector<std::string> out;
    out.swap(impl->status.friendEdits);
    return out;
}

}  // namespace citron
