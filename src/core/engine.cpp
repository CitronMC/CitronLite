// Toolhelp's ANSI structs are used here, whatever the rest of the build defines.
#ifdef UNICODE
#undef UNICODE
#endif
#ifdef _UNICODE
#undef _UNICODE
#endif
#include "citron/engine.h"
#include "citron/log.h"
#include <tlhelp32.h>
#include <cctype>
#include <memory>
#include <cstring>
#include <chrono>

namespace citron::engine {

DWORD FindPid(const char* exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe = {}; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (lstrcmpiA(pe.szExeFile, exe) == 0) { pid = pe.th32ProcessID; break; }
    CloseHandle(snap);
    return pid;
}

bool Attach(Proc& p, const char* exe) {
    Detach(p);
    p.pid = FindPid(exe);
    if (!p.pid) return false;

    // Only the rights used: PROCESS_ALL_ACCESS fails more often across the game's AppContainer.
    p.h = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
                      PROCESS_QUERY_INFORMATION, FALSE, p.pid);
    if (!p.h) {
        Log("[engine] OpenProcess failed pid=%lu err=%lu", p.pid, GetLastError());
        return false;
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, p.pid);
    if (snap == INVALID_HANDLE_VALUE) {
        Log("[engine] module snapshot failed pid=%lu err=%lu", p.pid, GetLastError());
        Detach(p); return false;
    }
    MODULEENTRY32 me = {}; me.dwSize = sizeof(me);
    for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me))
        if (lstrcmpiA(me.szModule, exe) == 0) { p.base = me.modBaseAddr; p.size = me.modBaseSize; break; }
    CloseHandle(snap);
    if (!p.base) { Log("[engine] module '%s' not found in pid=%lu", exe, p.pid); Detach(p); return false; }

    Log("[engine] attached pid=%lu base=0x%p size=%zu", p.pid, (void*)p.base, p.size);
    return true;
}

void Detach(Proc& p) {
    if (p.h) CloseHandle(p.h);
    p.h = nullptr; p.base = nullptr; p.size = 0; p.pid = 0;
    ResetCaveCache();
}

bool Alive(const Proc& p) {
    if (!p.h) return false;
    DWORD code = 0;
    return GetExitCodeProcess(p.h, &code) && code == STILL_ACTIVE;
}

bool Read(const Proc& p, uintptr_t addr, void* out, size_t n) {
    SIZE_T got = 0;
    return p.h && ReadProcessMemory(p.h, (LPCVOID)addr, out, n, &got) && got == n;
}
bool Write(const Proc& p, uintptr_t addr, const void* in, size_t n) {
    SIZE_T put = 0;
    return p.h && WriteProcessMemory(p.h, (LPVOID)addr, in, n, &put) && put == n;
}

namespace {

// WriteProcessMemory is not atomic. A thread whose instruction pointer is inside the bytes being
// replaced runs half the old instruction and half the new one and faults, always at the same place.
// Code is only ever written with every game thread stopped and none of them inside the range.
constexpr size_t kCaveSpan = 0x1000;   // the largest cave allocated here

class FrozenThreads {
public:
    explicit FrozenThreads(DWORD pid) {
        if (!pid) return;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID != pid) continue;
                HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
                if (!th) continue;
                if (SuspendThread(th) == (DWORD)-1) { CloseHandle(th); continue; }
                held.push_back(th);
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }
    ~FrozenThreads() {
        for (HANDLE th : held) { ResumeThread(th); CloseHandle(th); }
    }
    bool empty() const { return held.empty(); }
    // Whether a stopped thread is inside [addr, addr + n).
    bool insideRange(uintptr_t addr, size_t n) const {
        for (HANDLE th : held) {
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(th, &ctx)) continue;
            const uintptr_t rip = (uintptr_t)ctx.Rip;
            if (rip >= addr && rip < addr + n) return true;
        }
        return false;
    }
private:
    std::vector<HANDLE> held;
};

}  // namespace

bool WriteProtected(const Proc& p, uintptr_t addr, const void* in, size_t n) {
    if (!p.h || !in || n == 0) return false;
    // A thread parked on the bytes is rare; a few tries clear it, and if none does the write is
    // refused rather than risked.
    std::unique_ptr<FrozenThreads> freeze;
    for (int attempt = 0; attempt < 8; ++attempt) {
        freeze = std::make_unique<FrozenThreads>(p.pid);
        if (freeze->empty() || !freeze->insideRange(addr, n)) break;
        freeze.reset();
        Sleep(1);
        if (attempt == 7) {
            Log("[patch] refused 0x%llX (%zu bytes): a game thread is standing on it",
                (unsigned long long)addr, n);
            return false;
        }
    }
    DWORD oldProt = 0;
    if (!VirtualProtectEx(p.h, (LPVOID)addr, n, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    SIZE_T put = 0;
    const BOOL wrote = WriteProcessMemory(p.h, (LPVOID)addr, in, n, &put);
    const BOOL flushed = wrote && put == n && FlushInstructionCache(p.h, (LPCVOID)addr, n);
    DWORD tmp = 0;
    const BOOL restored = VirtualProtectEx(p.h, (LPVOID)addr, n, oldProt, &tmp);
    if (!wrote || put != n || !flushed || !restored) return false;
    std::vector<uint8_t> readback(n);
    return Read(p, addr, readback.data(), n) && std::memcmp(readback.data(), in, n) == 0;
}

std::vector<int> ParsePattern(const char* ida) {
    std::vector<int> out;
    for (const char* s = ida; *s; ) {
        if (*s == ' ') { ++s; continue; }
        if (*s == '?') { out.push_back(-1); while (*s == '?') ++s; continue; }
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            c = (char)std::tolower((unsigned char)c);
            if (c >= 'a' && c <= 'f') return 10 + c - 'a';
            return -1;
        };
        int hi = hex(s[0]); if (hi < 0) { ++s; continue; }
        int lo = hex(s[1]);
        if (lo < 0) { out.push_back(hi); ++s; }
        else { out.push_back((hi << 4) | lo); s += 2; }
    }
    return out;
}

bool ReadModule(const Proc& p, std::vector<uint8_t>& out) {
    if (!p.ok() || !p.size) return false;
    out.assign(p.size, 0);
    const size_t CH = 4u * 1024 * 1024;
    bool any = false;
    for (size_t off = 0; off < p.size; off += CH) {
        size_t n = (p.size - off < CH) ? (p.size - off) : CH;
        SIZE_T got = 0;
        if (ReadProcessMemory(p.h, (LPCVOID)(p.base + off), out.data() + off, n, &got) && got) any = true;
    }
    return any;
}

bool NextDataSlice(const Proc& p, uintptr_t& cursor, size_t budget, size_t overlap,
                   std::vector<uint8_t>& scratch, DataSlice& out) {
    out = {};
    constexpr uintptr_t kUserEnd = 0x7FFFFFFF0000ull;
    // Committed read/write private pages, regions up to 256 MiB. Mapped views are never read: on a
    // D3D12 game they include memory the graphics driver maps for the GPU, and reading that from
    // another process crashes the game. The options and everything else searched for live in the
    // private heap.
    constexpr size_t kMaxRegion = 256u << 20;
    if (!p.h || !budget) return false;

    MEMORY_BASIC_INFORMATION mbi{};
    while (cursor < kUserEnd && VirtualQueryEx(p.h, (LPCVOID)cursor, &mbi, sizeof(mbi))) {
        const uintptr_t base = (uintptr_t)mbi.BaseAddress;
        const uintptr_t end  = base + mbi.RegionSize;
        if (end <= cursor) break;
        if (mbi.State != MEM_COMMIT || mbi.Protect != PAGE_READWRITE ||
            mbi.Type != MEM_PRIVATE || mbi.RegionSize > kMaxRegion) {
            cursor = end;
            continue;
        }
        const uintptr_t from = cursor > base ? cursor : base;
        size_t take = (size_t)(end - from);
        if (take > budget) take = budget;
        size_t want = take + overlap;
        if (want > (size_t)(end - from)) want = (size_t)(end - from);
        scratch.resize(want);
        SIZE_T got = 0;
        if (!ReadProcessMemory(p.h, (LPCVOID)from, scratch.data(), want, &got) || !got) {
            cursor = end;   // freed while walking
            continue;
        }
        cursor = from + take;   // the whole slice, even after a short read
        out.address = from;
        out.data    = scratch.data();
        out.size    = (size_t)got > take ? take : (size_t)got;
        out.overlap = (size_t)got - out.size;
        return true;
    }
    cursor = kUserEnd;
    return false;
}

// Code caves. The first one is found by walking the memory map down from the site; each later one
// is placed 64 KB below the last, which is almost always free and one query away.
static HANDLE    g_caveProc = nullptr;
static uintptr_t g_caveNext = 0;

void ResetCaveCache() { g_caveProc = nullptr; g_caveNext = 0; }

void* AllocCaveNear(const Proc& p, uintptr_t nearAddr, size_t size) {
    if (!p.h) return nullptr;
    const uintptr_t LIMIT = 0x7F000000ull;                    // within a rel32 jump of nearAddr
    const uintptr_t STEP  = (size + 0xFFFFull) & ~0xFFFFull;  // allocation granularity
    MEMORY_BASIC_INFORMATION mbi;
    auto reserve = [&](uintptr_t a) -> void* {
        return VirtualAllocEx(p.h, (LPVOID)a, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    };

    if (p.h == g_caveProc && g_caveNext) {
        uintptr_t a = g_caveNext;
        uintptr_t d = nearAddr > a ? nearAddr - a : a - nearAddr;
        if (d <= LIMIT && VirtualQueryEx(p.h, (LPCVOID)a, &mbi, sizeof(mbi)) &&
            mbi.State == MEM_FREE && mbi.RegionSize >= size) {
            if (void* got = reserve(a)) { g_caveNext = (uintptr_t)got - STEP; return got; }
        }
    }

    auto t0 = std::chrono::steady_clock::now();
    int probes = 0;
    uintptr_t addr = nearAddr & ~0xFFFFull;
    for (int i = 0; i < 0x20000; ++i) {
        ++probes;
        if (!VirtualQueryEx(p.h, (LPCVOID)addr, &mbi, sizeof(mbi))) break;
        if (mbi.State == MEM_FREE && mbi.RegionSize >= size) {
            uintptr_t b = ((uintptr_t)mbi.BaseAddress + 0xFFFF) & ~0xFFFFull;
            if (nearAddr - b <= LIMIT) {
                if (void* got = reserve(b)) {
                    long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - t0).count();
                    Log("[cave] walk found free @ 0x%llX in %d probes, %ld ms",
                        (unsigned long long)(uintptr_t)got, probes, ms);
                    g_caveProc = p.h;
                    g_caveNext = (uintptr_t)got - STEP;
                    return got;
                }
            }
        }
        uintptr_t bb = (uintptr_t)mbi.BaseAddress;
        if (bb == 0) break;
        addr = (bb - 1) & ~0xFFFFull;
        if (nearAddr - addr > LIMIT) break;
    }
    return nullptr;
}

bool InstallCaptureRegHook(const Proc& p, CaveHook& h, uintptr_t match, int stolenN, uint8_t modrm, uint8_t rex) {
    if (!p.h || stolenN < 5 || stolenN > 16) return false;

    uint8_t orig[16] = {};
    if (!Read(p, match, orig, stolenN)) return false;

    const size_t CAVE = 0x108;
    void* cave = AllocCaveNear(p, match, CAVE);
    if (!cave) { Log("[cave] AllocCaveNear failed near 0x%llX", (unsigned long long)match); return false; }
    uintptr_t caveA = (uintptr_t)cave;
    uintptr_t slot  = caveA + 0x100;

    uint8_t buf[0x108] = {};
    int n = 0;
    // mov [rip+disp], reg
    buf[n++] = rex; buf[n++] = 0x89; buf[n++] = modrm;
    int32_t disp = (int32_t)(slot - (caveA + 7));
    std::memcpy(buf + n, &disp, 4); n += 4;
    // the stolen instructions, then back
    std::memcpy(buf + n, orig, stolenN); n += stolenN;
    buf[n++] = 0xE9;
    int32_t relBack = (int32_t)((match + stolenN) - (caveA + n + 4));
    std::memcpy(buf + n, &relBack, 4); n += 4;

    SIZE_T put = 0;
    if (!WriteProcessMemory(p.h, cave, buf, CAVE, &put) || put != CAVE) {
        VirtualFreeEx(p.h, cave, 0, MEM_RELEASE);
        Log("[cave] write cave body failed"); return false;
    }

    // the jump in, padded with NOPs to the stolen length
    uint8_t tramp[16]; std::memset(tramp, 0x90, sizeof(tramp));
    tramp[0] = 0xE9;
    int32_t relFwd = (int32_t)(caveA - (match + 5));
    std::memcpy(tramp + 1, &relFwd, 4);
    if (!WriteProtected(p, match, tramp, stolenN)) {
        VirtualFreeEx(p.h, cave, 0, MEM_RELEASE);
        Log("[cave] write trampoline failed"); return false;
    }

    h.match = match; h.cave = caveA; h.slot = slot; h.stolen = stolenN;
    std::memcpy(h.orig, orig, stolenN); h.installed = true;
    // The stolen bytes are logged: a length that splits an instruction shows up here at once.
    {
        char hex[3 * 16 + 1]; int at = 0;
        for (int i = 0; i < stolenN && i < 16; ++i)
            at += std::snprintf(hex + at, sizeof(hex) - at, "%02X ", orig[i]);
        Log("[cave] installed @ 0x%llX -> cave 0x%llX slot 0x%llX (stole %d: %s)",
            (unsigned long long)match, (unsigned long long)caveA,
            (unsigned long long)slot, stolenN, hex);
    }
    return true;
}

bool InstallXmm0ScaleHook(const Proc& p, CaveHook& h, uintptr_t match, int stolenN,
                          float initial) {
    if (!p.h || stolenN < 5 || stolenN > 16) return false;

    uint8_t orig[16] = {};
    if (!Read(p, match, orig, stolenN)) return false;

    const size_t CAVE = 0x108;
    void* cave = AllocCaveNear(p, match, CAVE);
    if (!cave) { Log("[cave] override AllocCaveNear failed near 0x%llX", (unsigned long long)match); return false; }
    uintptr_t caveA = (uintptr_t)cave;
    uintptr_t slot  = caveA + 0x100;

    uint8_t buf[0x108] = {};
    int n = 0;
    // mulss xmm0, [factor]: the game's number scaled, not replaced, since it changes with state
    // (sprinting raises it). 1.0 stores exactly what the game would have.
    buf[n++] = 0xF3; buf[n++] = 0x0F; buf[n++] = 0x59; buf[n++] = 0x05;
    int32_t disp = (int32_t)(slot - (caveA + 8));
    std::memcpy(buf + n, &disp, 4); n += 4;
    // the game's own store, then back
    std::memcpy(buf + n, orig, stolenN); n += stolenN;
    buf[n++] = 0xE9;
    int32_t relBack = (int32_t)((match + stolenN) - (caveA + n + 4));
    std::memcpy(buf + n, &relBack, 4); n += 4;
    // the factor is in place before the jump in is
    std::memcpy(buf + 0x100, &initial, sizeof(initial));

    SIZE_T put = 0;
    if (!WriteProcessMemory(p.h, cave, buf, CAVE, &put) || put != CAVE) {
        VirtualFreeEx(p.h, cave, 0, MEM_RELEASE);
        Log("[cave] override write cave body failed"); return false;
    }

    uint8_t tramp[16]; std::memset(tramp, 0x90, sizeof(tramp));
    tramp[0] = 0xE9;
    int32_t relFwd = (int32_t)(caveA - (match + 5));
    std::memcpy(tramp + 1, &relFwd, 4);
    if (!WriteProtected(p, match, tramp, stolenN)) {
        VirtualFreeEx(p.h, cave, 0, MEM_RELEASE);
        Log("[cave] override write trampoline failed"); return false;
    }

    h.match = match; h.cave = caveA; h.slot = slot; h.stolen = stolenN;
    std::memcpy(h.orig, orig, stolenN); h.installed = true;
    Log("[cave] xmm0-scale installed @ 0x%llX -> cave 0x%llX slot 0x%llX (stole %d, factor %.3f)",
        (unsigned long long)match, (unsigned long long)caveA,
        (unsigned long long)slot, stolenN, initial);
    return true;
}

void RemoveHook(const Proc& p, CaveHook& h) {
    if (!h.installed) return;
    if (!WriteProtected(p, h.match, h.orig, h.stolen)) {
        // The site still jumps into the cave, so the cave stays.
        Log("[cave] site 0x%llX could not be restored; keeping the cave mapped",
            (unsigned long long)h.match);
        return;
    }
    // A thread that entered the cave a moment ago may still be in it; then it stays mapped.
    if (h.cave) {
        const FrozenThreads freeze(p.pid);
        if (freeze.empty() || !freeze.insideRange(h.cave, kCaveSpan))
            VirtualFreeEx(p.h, (LPVOID)h.cave, 0, MEM_RELEASE);
        else
            Log("[cave] 0x%llX still has a thread inside it; left mapped",
                (unsigned long long)h.cave);
    }
    Log("[cave] removed @ 0x%llX", (unsigned long long)h.match);
    h.installed = false; h.cave = 0; h.slot = 0;
}

// A site left jumping into a cave by a run that ended without putting it back. Every cave ends
// with the stolen bytes and then the jump back to site + stolen, so the cave itself says what the
// site held. Returns the bytes restored, 0 when the site was not left that way.
int HealStaleCave(const Proc& p, uintptr_t site) {
    uint8_t b0 = 0;
    if (!Read(p, site, b0) || b0 != 0xE9) return 0;
    int32_t rel = 0; if (!Read(p, site + 1, rel)) return 0;
    uintptr_t cave = site + 5 + (int64_t)rel;
    uint8_t buf[0x100];
    if (!Read(p, cave, buf, sizeof(buf))) return 0;         // the code; slots start at +0x100
    for (int i = (int)sizeof(buf) - 5; i >= 5; --i) {
        if (buf[i] != 0xE9) continue;
        int32_t r; std::memcpy(&r, buf + i + 1, 4);
        uintptr_t tgt = cave + i + 5 + (int64_t)r;
        long stolenN = (long)((int64_t)tgt - (int64_t)site);
        if (stolenN < 5 || stolenN > 16 || i < stolenN) continue;
        const uint8_t* orig = buf + (i - stolenN);
        if (orig[0] == 0xE9) continue;                      // never reinstate another jump
        if (!WriteProtected(p, site, orig, (int)stolenN)) return 0;
        Log("[heal] restored stale cave @ 0x%llX (%ld bytes) from cave 0x%llX",
            (unsigned long long)site, stolenN, (unsigned long long)cave);
        return (int)stolenN;
    }
    return 0;
}

} // namespace citron::engine
