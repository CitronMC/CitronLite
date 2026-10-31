// The caves that decide things, as machine code: each real cave goes into a stand-in function in another
// process, with the game function's own first instructions, and the stand-in is called every way.
//   cave_tests        starts itself as the stand-in, then tests it
#include "citron/engine.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// What the two processes share, in a named mapping.
struct Shared {
    uint64_t entry;          // the stand-in's address in its process
    uint64_t grab, release;  // the cursor's stand-ins
    int grabs, releases;     // how many times each has run
    volatile LONG ready;
    volatile LONG ask;       // 1: run `slot`, 2: grab the cursor, 0 when answered
    int from;                // the slot held before the call
    int slot;
    int result;              // what the stand-in returned
    int held;                // what its inventory holds after the call
    volatile LONG stop;
};

constexpr wchar_t kMapName[] = L"Local\\CitronLiteCaveTest";

// push rbp; push r15; push r14; push r13; then the slot taken: mov [rcx+0x10], edx; mov eax, 1;
// and back through the same pops. The first seven bytes are what the guard takes the place of.
const uint8_t kStandIn[] = {
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,   // push rbp, r15, r14, r13
    0x89, 0x51, 0x10,                           // mov [rcx+0x10], edx
    0xB8, 0x01, 0x00, 0x00, 0x00,               // mov eax, 1
    0x41, 0x5D, 0x41, 0x5E, 0x41, 0x5F, 0x5D,   // pop r13, r14, r15, rbp
    0xC3,                                       // ret
};

// push rsi; sub rsp, 0x20 as grabCursor opens, then a count at [rcx+0x14] and back
const uint8_t kGrab[] = {0x56, 0x48, 0x83, 0xEC, 0x20, 0xFF, 0x41, 0x14, 0x48, 0x83, 0xC4, 0x20, 0x5E, 0xC3};
// a count at [rcx+0x18]
const uint8_t kRelease[] = {0xFF, 0x41, 0x18, 0xC3};

using SelectSlot = int (*)(void* inventory, int slot, int flag);
using Grab = void (*)(void* self);

int Child() {
    HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kMapName);
    if (!map) return 2;
    auto* s = static_cast<Shared*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    if (!s) return 2;
    // near the image, as the game's code is, so a cave can be reached by a rel32 jump
    uint8_t* code = nullptr;
    const uintptr_t self = (uintptr_t)GetModuleHandleW(nullptr);
    for (uintptr_t at = (self + 0x10000000) & ~0xFFFFull; at < self + 0x70000000 && !code; at += 0x100000)
        code = static_cast<uint8_t*>(VirtualAlloc((void*)at, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!code) return 3;
    std::memcpy(code, kStandIn, sizeof kStandIn);
    std::memcpy(code + 0x40, kGrab, sizeof kGrab);
    std::memcpy(code + 0x80, kRelease, sizeof kRelease);
    FlushInstructionCache(GetCurrentProcess(), code, 0x100);
    s->entry = (uint64_t)code;
    s->grab = (uint64_t)code + 0x40;
    s->release = (uint64_t)code + 0x80;
    uint8_t inventory[0x20] = {};
    int* held = reinterpret_cast<int*>(inventory + 0x10);
    InterlockedExchange(&s->ready, 1);
    while (!s->stop) {
        if (s->ask == 2) {
            reinterpret_cast<Grab>(code + 0x40)(inventory);
            s->grabs = *reinterpret_cast<int*>(inventory + 0x14);
            s->releases = *reinterpret_cast<int*>(inventory + 0x18);
            InterlockedExchange(&s->ask, 0);
            continue;
        }
        if (s->ask != 1) {
            Sleep(0);
            continue;
        }
        *held = s->from;
        s->result = reinterpret_cast<SelectSlot>(code)(inventory, s->slot, 0);
        s->held = *held;
        InterlockedExchange(&s->ask, 0);
    }
    return 0;
}

int g_failed = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-58s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    g_failed += !ok;
}

struct Call { int result, held; };

Call Ask(Shared* s, int slot, int from = 4) {
    s->from = from;
    s->slot = slot;
    InterlockedExchange(&s->ask, 1);
    while (s->ask == 1) Sleep(0);
    return {s->result, s->held};
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--stand-in") == 0) return Child();

    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), kMapName);
    auto* s = static_cast<Shared*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    std::memset(s, 0, sizeof *s);

    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string cmd = std::string("\"") + exe + "\" --stand-in";
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        std::printf("could not start the stand-in\n");
        return 1;
    }
    for (int i = 0; i < 2000 && !s->ready; ++i) Sleep(1);
    Check(s->ready != 0, "the stand-in is up");

    using namespace citron::engine;
    Proc proc;
    proc.h = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, pi.dwProcessId);
    proc.pid = pi.dwProcessId;
    proc.base = reinterpret_cast<uint8_t*>(s->entry);
    proc.size = sizeof kStandIn;
    Check(proc.h != nullptr, "opened it");

    Call c = Ask(s, 5);
    Check(c.result == 1 && c.held == 5, "before the guard a step goes through");

    CaveHook hook, cursor;
    const bool in = InstallSlotGuardHook(proc, hook, s->entry, 7);
    Check(in, "the guard goes in");
    c = Ask(s, 5);
    Check(c.result == 1 && c.held == 5, "off, a step goes through");
    c = Ask(s, 7);
    Check(c.result == 1 && c.held == 7, "off, a jump goes through");

    const uint8_t on = 1, off = 0;
    Check(Write(proc, hook.slot, on), "the byte is set");
    for (int slot : {3, 5}) {   // the stand-in holds slot 4 before each call
        c = Ask(s, slot);
        Check(c.result == 0 && c.held == 4, "on, a step of one to slot " + std::to_string(slot) + " is refused");
    }
    for (int slot : {0, 1, 2, 4, 6, 7, 8}) {
        c = Ask(s, slot);
        Check(c.result == 1 && c.held == slot, "on, slot " + std::to_string(slot) + " from 4 goes through");
    }
    c = Ask(s, 8, 0);
    Check(c.result == 0 && c.held == 0, "on, a step round the end, 0 to 8, is refused");
    c = Ask(s, 0, 8);
    Check(c.result == 0 && c.held == 8, "on, a step round the end, 8 to 0, is refused");
    Check(Write(proc, hook.slot, off), "the byte is cleared");
    c = Ask(s, 5);
    Check(c.result == 1 && c.held == 5, "off again, a step goes through");

    RemoveHook(proc, hook);
    Check(!hook.installed, "the guard comes out");
    uint8_t back[sizeof kStandIn] = {};
    Check(Read(proc, s->entry, back, sizeof back) && std::memcmp(back, kStandIn, sizeof back) == 0, "the code is as it was");
    c = Ask(s, 3);
    Check(c.result == 1 && c.held == 3, "after it a step goes through");

    // the cursor: what each state makes of a grab
    auto tick = [&] {
        InterlockedExchange(&s->ask, 2);
        while (s->ask == 2) Sleep(0);
    };
    auto state = [&] {
        uint8_t now = 0xFF;
        Read(proc, cursor.slot, now);
        return now;
    };
    Check(InstallCursorGuardHook(proc, cursor, s->grab, 5, s->release), "the cursor's guard goes in");
    tick();
    Check(s->grabs == 1 && s->releases == 0, "plain, the game grabs");
    Write(proc, cursor.slot, kCursorRelease);
    tick();
    Check(s->grabs == 1 && s->releases == 1, "asked to, the grab lets go instead");
    Check(state() == kCursorFree, "and the state moves on to free");
    tick();
    tick();
    Check(s->grabs == 1 && s->releases == 1, "free, the pointer is left alone, and it lets go only once");
    Write(proc, cursor.slot, kCursorGrabAgain);
    tick();
    Check(s->grabs == 2 && s->releases == 1, "closing, the game grabs");
    Check(state() == kCursorPlain, "and the state says it has");
    tick();
    Check(s->grabs == 3 && s->releases == 1, "plain again, the game grabs");
    RemoveHook(proc, cursor);
    uint8_t grabBack[sizeof kGrab] = {};
    Check(Read(proc, s->grab, grabBack, sizeof grabBack) && std::memcmp(grabBack, kGrab, sizeof grabBack) == 0, "its code is as it was");

    InterlockedExchange(&s->stop, 1);
    WaitForSingleObject(pi.hProcess, 3000);
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (g_failed) {
        std::printf("%d checks failed\n", g_failed);
        return 1;
    }
    std::printf("cave tests passed\n");
    return 0;
}
