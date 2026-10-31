// The window over the game: transparent, topmost, and never in the way. It takes no focus, and clicks
// pass through it unless it is made interactive. It shows only while it has something on it, because a
// window over the game keeps the game from flipping straight to the screen.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>

struct ImFontAtlas;

namespace citron::overlay {

// With the ImGui context of the main window current. Fonts are shared with it. False when the system
// has no composition; the client then runs without an overlay.
bool Init(ID3D11Device* device, ID3D11DeviceContext* context, ImFontAtlas* fonts);
void Shutdown();

// Over the client area of `target`, or hidden when it is null.
void Sync(HWND target, bool interactive);
bool Visible();
float Scale();                 // the target's monitor scale
void SetCaptureHidden(bool hidden);

// One frame, with the overlay's ImGui context current between the two calls.
void BeginFrame();
void EndFrame();

}  // namespace citron::overlay
