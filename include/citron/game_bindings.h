#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace citron::gamebind {

// Bedrock stores normal keyboard bindings as Win32 virtual-key values. Its three mouse buttons use
// the stable negative action codes -99 (left), -98 (right) and -97 (middle).
struct Bindings {
    int attack = -99;
    int use = -98;
    int hotbar[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    int forward = 'W';
    int back = 'S';
    int left = 'A';
    int right = 'D';
    int jump = VK_SPACE;
    int sneak = VK_SHIFT;
    int sprint = VK_CONTROL;
    int inventory = 'E';
    int chat = 'T';
    // Bedrock's options.txt calls this "drop". Parsed rather than assumed to be Q, because a
    // module that throws items away must use the key the player actually rebound.
    int drop = 'Q';
    bool loaded = false;
};

// Resolve the active Bedrock options.txt for `minecraftProcess` and cache it. The cache checks for a
// changed options file every two seconds, so bindings edited while Citron is running are picked up.
Bindings Resolve(HANDLE minecraftProcess);

// Send/query a resolved Bedrock input code. Unsupported/unbound codes fail closed.
bool Send(int code, bool down);
bool Down(int code);

} // namespace citron::gamebind
