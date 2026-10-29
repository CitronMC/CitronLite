// Reading and patching another process: attach, memory reads and writes, pattern parsing, the heap
// walk and code caves.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>

namespace citron::engine {

struct Proc {
    HANDLE   h    = nullptr;
    uint8_t* base = nullptr;     // the game's image
    size_t   size = 0;
    DWORD    pid  = 0;
    bool ok() const { return h && base; }
};

DWORD FindPid(const char* exe);
bool  Attach(Proc& p, const char* exe);
void  Detach(Proc& p);
bool  Alive(const Proc& p);

bool Read (const Proc& p, uintptr_t addr, void* out, size_t n);
bool Write(const Proc& p, uintptr_t addr, const void* in, size_t n);
// For code and read-only data: lifts the page protection for the write, with the game's threads
// stopped so none of them runs a half-written instruction.
bool WriteProtected(const Proc& p, uintptr_t addr, const void* in, size_t n);
template <class T> bool Read (const Proc& p, uintptr_t a, T& v) { return Read (p, a, &v, sizeof(T)); }
template <class T> bool Write(const Proc& p, uintptr_t a, const T& v) { return Write(p, a, &v, sizeof(T)); }

// "74 0D ?? F3" to one int a byte, -1 for a wildcard.
std::vector<int> ParsePattern(const char* ida);
// The whole image, unreadable pages left zero.
bool ReadModule(const Proc& p, std::vector<uint8_t>& out);

// The other modules loaded in the game: Reach Spoofer patches another client's DLL, not the game.
struct ModuleInfo {
    std::string name;
    uintptr_t   base = 0;
    size_t      size = 0;
    bool        system = false;   // under the Windows directory, never a target
};
std::vector<ModuleInfo> EnumModules(DWORD pid);
bool ReadRegion(const Proc& p, uintptr_t base, size_t size, std::vector<uint8_t>& out);

// A walk over the game's private read/write memory, for values that live only in the heap. It
// hands back one slice a call so a search can share the worker with everything else.
struct DataSlice {
    uintptr_t      address = 0;       // where these bytes are in the game
    const uint8_t* data    = nullptr; // in the caller's scratch, until the next call
    size_t         size    = 0;       // a match may start anywhere in here
    size_t         overlap = 0;       // readable bytes past `size`, so a match can cross the seam
};

// The next slice from `cursor` (start at 0); false once the walk is done. `overlap` is the longest
// needle minus one.
bool NextDataSlice(const Proc& p, uintptr_t& cursor, size_t budget, size_t overlap,
                   std::vector<uint8_t>& scratch, DataSlice& out);

// A code cave: the site jumps to memory of ours, which does its work, runs the instructions it took
// the place of and jumps back. What it captures is at `slot`, cave + 0x100.
struct CaveHook {
    uintptr_t match = 0;     // the site
    uintptr_t cave  = 0;
    uintptr_t slot  = 0;
    int       ringN = 0;     // 0 for one value, else a ring of N pointers at slot + 8
    int       stolen = 0;    // whole instructions at the site, at least 5 bytes
    uint8_t   orig[16] = {};
    bool      installed = false;
};

// Memory within a rel32 jump of `nearAddr`, or nullptr.
void* AllocCaveNear(const Proc& p, uintptr_t nearAddr, size_t size);
void  ResetCaveCache();

// Stores a register in the slot each time the site runs. `modrm` is the /r byte of
// `mov [rip+disp], reg` (rax 0x05, rcx 0x0D, rdx 0x15, rbx 0x1D, rsi 0x35, rdi 0x3D) and `rex` its
// prefix: 0x48 for rax to rdi, 0x4C for r8 to r15.
bool InstallCaptureRegHook(const Proc& p, CaveHook& h, uintptr_t match, int stolenN, uint8_t modrm, uint8_t rex = 0x48);
inline bool InstallCaptureRaxHook(const Proc& p, CaveHook& h, uintptr_t match, int stolenN) {
    return InstallCaptureRegHook(p, h, match, stolenN, 0x05);
}
inline bool InstallCaptureR8Hook(const Proc& p, CaveHook& h, uintptr_t match, int stolenN) {
    return InstallCaptureRegHook(p, h, match, stolenN, 0x05, 0x4C);
}

// At a store of xmm0: multiplies xmm0 by the float at the slot first, so the game stores the
// scaled value itself. 1.0 changes nothing.
bool InstallXmm0ScaleHook(const Proc& p, CaveHook& h, uintptr_t match, int stolenN, float initial);

// Stores a register into a ring of `ringN` pointers (a power of two), for a site that runs for
// many objects a frame. The write count is at the slot, the ring at slot + 8.
bool InstallRingHook(const Proc& p, CaveHook& h, uintptr_t match, int stolenN, uint8_t modrm, int ringN);

// At a store of xmm0: xmm0 = min(xmm0 * mult, cap) first. The multiplier is at the slot, the cap at
// slot + 4, both live.
bool InstallXmm0CapHook(const Proc& p, CaveHook& h, uintptr_t match, int stolenN,
                        float mult, float cap);

// Puts the site back and frees the cave once no thread is inside it.
void RemoveHook(const Proc& p, CaveHook& h);

// Restores a site an earlier run left jumping into its cave. Returns the bytes restored, 0 if none.
int HealStaleCave(const Proc& p, uintptr_t site);

} // namespace citron::engine
