// The window: borderless, drawn with D3D11 and composited with rounded corners.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>

namespace citron::platform {

// Posted to the window to change how it shows: bit 0 takes it off the taskbar, bit 1 hides it
// altogether; 0 brings back both. The window's own thread has to make the change for the taskbar
// to notice it.
constexpr UINT WM_CITRON_TASKBARHIDE     = WM_APP + 0x21;
constexpr WPARAM CITRON_TASKBAR_TOOLWINDOW = 0x1;
constexpr WPARAM CITRON_TASKBAR_HIDEWINDOW = 0x2;

struct Window {
    HWND                    hwnd       = nullptr;
    ID3D11Device*           device     = nullptr;
    ID3D11DeviceContext*    context    = nullptr;
    IDXGISwapChain*         swapchain  = nullptr;
    ID3D11RenderTargetView* rtv        = nullptr;
    int  width  = 0;
    int  height = 0;
    int  designWidth  = 0;                 // the layout's own size; the window scales it
    int  designHeight = 0;
    float scale = 1.f;                     // window size over design size
    bool wantClose = false;

    // Windows runs its own loop while a border is dragged, so the window draws itself from the
    // WM_SIZE messages of that loop through this callback.
    bool  inSizeMove = false;
    void* frameCtx   = nullptr;
    void  (*onFrame)(Window&, void*) = nullptr;

    // Each frame is copied to the CPU, premultiplied with antialiased corners and handed to
    // UpdateLayeredWindow.
    ID3D11Texture2D* staging = nullptr;
    HDC              memDC   = nullptr;
    void*            dib     = nullptr;    // HBITMAP
    void*            dibBits = nullptr;    // top-down BGRA
    int   layW = 0, layH = 0;
    float cornerR   = 0.f;
    float baseCornerR = 0.f;               // at the design size
    int   constAlpha = 255;
    bool  perPixelAlpha = false;           // use the frame's alpha, not just the corners
    bool  vsyncFlush = true;               // wait for the compositor after each frame
};

// width and height are the design size; initialScale opens the window that much larger (the
// monitor's scale) so it is sharp from the first frame. An offscreen window is for captures and test
// sessions: past the desktop's edge, it never takes the focus and has no taskbar button.
bool Init(Window& w, int width, int height, const wchar_t* title, float cornerRadius,
          float initialScale = 1.f, bool offscreen = false);
// Brings the instance already running to the front, Taskbar Hide or not.
bool ActivateExistingInstance(unsigned waitMs = 0);

// Whether the window in front is Bedrock's. `gamePid` is the attached game, 0 for none. A title
// alone proves nothing (a browser tab can be called Minecraft), so only the game's own process or
// the UWP frame host showing it count; anything unclear is false.
bool ForegroundIsBedrock(unsigned long gamePid);
bool Pump(Window& w);                                   // false once the app should quit
void BeginRender(Window& w, const float clearRGBA[4]);
void EndRender(Window& w);
void Shutdown(Window& w);

// The ImGui context this window's messages go to.
void SetWndProcImGuiContext(void* imguiContext);

void BeginDrag(Window& w);
void Minimize(Window& w);

} // namespace citron::platform
