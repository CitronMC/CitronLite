#include "citron/platform.h"
#include "citron/log.h"
#include "imgui.h"
#include "imgui_internal.h"   // ClearActiveID, on losing focus
#include "backends/imgui_impl_win32.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace citron::platform {
namespace {

constexpr wchar_t kClass[] = L"CitronLiteWindow";
ImGuiContext* g_wndprocCtx = nullptr;

void CreateRTV(Window& w) {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(w.swapchain->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
        w.device->CreateRenderTargetView(back, nullptr, &w.rtv);
        back->Release();
    }
}
void ReleaseRTV(Window& w) {
    if (w.rtv) { w.rtv->Release(); w.rtv = nullptr; }
}

constexpr float kMinWindowScale = 0.75f;

void LockSizingAspect(Window& w, WPARAM edge, RECT& rect) {
    if (w.designWidth <= 0 || w.designHeight <= 0) return;
    const int proposedW = std::max(1L, rect.right - rect.left);
    const int proposedH = std::max(1L, rect.bottom - rect.top);
    const float aspect = (float)w.designWidth / (float)w.designHeight;
    float scale = ((float)proposedW / (float)proposedH >= aspect)
        ? (float)proposedW / (float)w.designWidth
        : (float)proposedH / (float)w.designHeight;
    scale = std::max(scale, kMinWindowScale);
    const int targetW = (int)std::lround((float)w.designWidth * scale);
    const int targetH = (int)std::lround((float)w.designHeight * scale);

    switch (edge) {
    case WMSZ_TOPLEFT:
        rect.left = rect.right - targetW;
        rect.top = rect.bottom - targetH;
        break;
    case WMSZ_TOPRIGHT:
        rect.right = rect.left + targetW;
        rect.top = rect.bottom - targetH;
        break;
    case WMSZ_BOTTOMLEFT:
        rect.left = rect.right - targetW;
        rect.bottom = rect.top + targetH;
        break;
    case WMSZ_BOTTOMRIGHT:
        rect.right = rect.left + targetW;
        rect.bottom = rect.top + targetH;
        break;
    // An edge sets one side; the other grows from a fixed corner so the window keeps its shape.
    case WMSZ_LEFT:
        rect.left = rect.right - targetW;
        rect.bottom = rect.top + targetH;
        break;
    case WMSZ_RIGHT:
        rect.right = rect.left + targetW;
        rect.bottom = rect.top + targetH;
        break;
    case WMSZ_TOP:
        rect.top = rect.bottom - targetH;
        rect.right = rect.left + targetW;
        break;
    case WMSZ_BOTTOM:
        rect.bottom = rect.top + targetH;
        rect.right = rect.left + targetW;
        break;
    default:
        break;
    }
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Window* w = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    // The taskbar only notices the style change if the window is hidden across it.
    if (msg == WM_CITRON_TASKBARHIDE) {
        const bool toolwindow = (wp & citron::platform::CITRON_TASKBAR_TOOLWINDOW) != 0;
        const bool hideWindow = (wp & citron::platform::CITRON_TASKBAR_HIDEWINDOW) != 0;
        const bool wasVisible = IsWindowVisible(hwnd) != FALSE;
        const bool wasIconic = IsIconic(hwnd) != FALSE;
        const bool wasForeground = GetForegroundWindow() == hwnd;
        ShowWindow(hwnd, SW_HIDE);

        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (toolwindow) ex = (ex & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW;
        else            ex = (ex & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

        // Hidden altogether, it stays hidden until the bind is pressed again. Otherwise it comes back
        // as it was, never opening a minimized window or taking focus from the game.
        if (!hideWindow) {
            if (wasVisible) {
                const int showCmd = wasIconic ? SW_SHOWMINNOACTIVE
                                  : (wasForeground ? SW_SHOW : SW_SHOWNOACTIVATE);
                ShowWindow(hwnd, showCmd);
                if (wasForeground && !wasIconic) SetForegroundWindow(hwnd);
            } else {
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            }
        }
        return 0;
    }

    // Losing focus ends any typing: ImGui's own handler releases the keys but keeps the active item.
    if (msg == WM_KILLFOCUS && g_wndprocCtx && ImGui::GetCurrentContext()) {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(g_wndprocCtx);
        ImGui::ClearActiveID();
        ImGui::SetCurrentContext(prev);
    }

    if (g_wndprocCtx && ImGui::GetCurrentContext()) {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(g_wndprocCtx);
        bool handled = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
        ImGui::SetCurrentContext(prev);
        if (handled) return true;
    }

    switch (msg) {
    case WM_NCCALCSIZE:
        // WS_THICKFRAME for the native sizing, but every pixel is ours.
        if (wp) return 0;
        break;
    case WM_NCHITTEST:
        if (w) {
            POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &p);
            // Wide corner grips and thin edges, clear of the window controls.
            const int corner = std::clamp((int)std::lround(16.f * w->scale), 14, 34);
            const int edge   = std::clamp((int)std::lround(6.f  * w->scale),  5, 14);
            const bool lC = p.x >= 0 && p.x < corner;
            const bool rC = p.x < w->width  && p.x >= w->width  - corner;
            const bool tC = p.y >= 0 && p.y < corner;
            const bool bC = p.y < w->height && p.y >= w->height - corner;
            if (tC && lC) return HTTOPLEFT;
            if (tC && rC) return HTTOPRIGHT;
            if (bC && lC) return HTBOTTOMLEFT;
            if (bC && rC) return HTBOTTOMRIGHT;
            if (p.x >= 0 && p.x < edge)                 return HTLEFT;
            if (p.x < w->width  && p.x >= w->width  - edge) return HTRIGHT;
            if (p.y >= 0 && p.y < edge)                 return HTTOP;
            if (p.y < w->height && p.y >= w->height - edge) return HTBOTTOM;
            return HTCLIENT;
        }
        break;
    case WM_SIZING:
        if (w && lp) {
            LockSizingAspect(*w, wp, *reinterpret_cast<RECT*>(lp));
            return TRUE;
        }
        break;
    case WM_GETMINMAXINFO:
        if (w && lp && w->designWidth > 0 && w->designHeight > 0) {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = (LONG)std::lround(w->designWidth * kMinWindowScale);
            mm->ptMinTrackSize.y = (LONG)std::lround(w->designHeight * kMinWindowScale);
            return 0;
        }
        break;
    case WM_DPICHANGED:
        // Moved to a monitor with another scale: take the size Windows suggests.
        if (lp) {
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top,
                         r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        break;
    case WM_ENTERSIZEMOVE:
        if (w) w->inSizeMove = true;
        return 0;
    case WM_EXITSIZEMOVE:
        if (w) w->inSizeMove = false;
        return 0;
    case WM_SIZE:
        if (w && w->device && w->swapchain && wp != SIZE_MINIMIZED) {
            ReleaseRTV(*w);
            w->width  = LOWORD(lp);
            w->height = HIWORD(lp);
            if (w->designWidth > 0) w->scale = (float)w->width / (float)w->designWidth;
            w->cornerR = w->baseCornerR * w->scale;
            w->swapchain->ResizeBuffers(0, w->width, w->height, DXGI_FORMAT_UNKNOWN, 0);
            CreateRTV(*w);
            if (w->inSizeMove && w->onFrame) w->onFrame(*w, w->frameCtx);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;   // swallow Alt menu
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        if (w) w->wantClose = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

bool ForegroundIsBedrock(unsigned long gamePid) {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (gamePid && pid == gamePid) return true;

    char base[64] = {};
    if (HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        char img[MAX_PATH] = {};
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameA(hp, 0, img, &n)) {
            const char* b = strrchr(img, '\\');
            lstrcpynA(base, b ? b + 1 : img, sizeof(base));
        }
        CloseHandle(hp);
    }
    if (lstrcmpiA(base, "javaw.exe") == 0 || lstrcmpiA(base, "java.exe") == 0) return false;
    if (lstrcmpiA(base, "Minecraft.Windows.exe") == 0) return true;
    // Only the UWP frame host may vouch for a title; an unreadable image name lands here too.
    if (lstrcmpiA(base, "ApplicationFrameHost.exe") != 0) return false;
    char title[256] = {};
    GetWindowTextA(fg, title, sizeof(title));
    return strstr(title, "Minecraft") != nullptr;
}

bool ActivateExistingInstance(unsigned waitMs) {
    const DWORD start = GetTickCount();
    HWND existing = nullptr;
    do {
        existing = FindWindowW(kClass, nullptr);
        if (existing) break;
        if (GetTickCount() - start >= waitMs) break;
        Sleep(25);
    } while (true);
    if (!existing) return false;

    if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
    else ShowWindow(existing, SW_SHOW);
    SetWindowPos(existing, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(existing);
    return true;
}

bool Init(Window& w, int width, int height, const wchar_t* title, float cornerRadius,
          float initialScale) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    // the icon from citron.rc, large and small
    wc.hIcon   = (HICON)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    RegisterClassExW(&wc);

    const float s = std::max(initialScale, kMinWindowScale);
    w.designWidth = width; w.designHeight = height;
    w.width  = (int)std::lround((float)width  * s);
    w.height = (int)std::lround((float)height * s);
    w.scale  = s;

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - w.width) / 2, y = (sh - w.height) / 2;

    w.hwnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_LAYERED, kClass, title,
        WS_POPUP | WS_THICKFRAME, x, y, w.width, w.height,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!w.hwnd) return false;

    SetWindowLongPtrW(w.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&w));

    w.baseCornerR = cornerRadius;
    w.cornerR    = cornerRadius * s;
    w.constAlpha = 255;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount       = 2;
    sd.BufferDesc.Width  = w.width;
    sd.BufferDesc.Height = w.height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = w.hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed     = TRUE;
    sd.SwapEffect   = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        want, 2, D3D11_SDK_VERSION, &sd,
        &w.swapchain, &w.device, &fl, &w.context);
    if (FAILED(hr)) return false;

    CreateRTV(w);

    ShowWindow(w.hwnd, SW_SHOW);
    UpdateWindow(w.hwnd);
    return true;
}

bool Pump(Window& w) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) return false;
    }
    return !w.wantClose;
}

void BeginRender(Window& w, const float clearRGBA[4]) {
    w.context->OMSetRenderTargets(1, &w.rtv, nullptr);
    w.context->ClearRenderTargetView(w.rtv, clearRGBA);
}
// The staging texture and the DIB the frame is composited through, rebuilt when the size changes.
static bool EnsureLayerBuffers(Window& w) {
    if (w.staging && w.layW == w.width && w.layH == w.height) return true;
    if (w.staging) { w.staging->Release(); w.staging = nullptr; }
    if (w.memDC)   { DeleteDC(w.memDC); w.memDC = nullptr; }
    if (w.dib)     { DeleteObject((HBITMAP)w.dib); w.dib = nullptr; }
    w.dibBits = nullptr; w.layW = w.layH = 0;

    ID3D11Texture2D* back = nullptr;
    if (FAILED(w.swapchain->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return false;
    D3D11_TEXTURE2D_DESC d; back->GetDesc(&d); back->Release();
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    if (FAILED(w.device->CreateTexture2D(&d, nullptr, &w.staging)) || !w.staging) return false;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w.width;
    bi.bmiHeader.biHeight      = -w.height;     // top-down, as D3D has it
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    w.memDC = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &w.dibBits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!w.memDC || !bmp || !w.dibBits) { if (bmp) DeleteObject(bmp); return false; }
    SelectObject(w.memDC, bmp);
    w.dib  = bmp;
    w.layW = w.width; w.layH = w.height;
    return true;
}

void EndRender(Window& w) {
    if (!EnsureLayerBuffers(w)) {          // no compositing: a plain, square window
        SetLayeredWindowAttributes(w.hwnd, 0, 255, LWA_ALPHA);
        w.swapchain->Present(1, 0);
        return;
    }

    ID3D11Texture2D* back = nullptr;
    if (FAILED(w.swapchain->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) {
        if (w.vsyncFlush) DwmFlush();
        return;
    }
    w.context->CopyResource(w.staging, back);
    back->Release();

    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(w.context->Map(w.staging, 0, D3D11_MAP_READ, 0, &m))) {
        const int   W = w.layW, H = w.layH;
        const float r = w.cornerR;
        const bool  ppa = w.perPixelAlpha;
        for (int y = 0; y < H; y++) {
            const unsigned char* srow = (const unsigned char*)m.pData + (size_t)y * m.RowPitch;
            unsigned char*       drow = (unsigned char*)w.dibBits + (size_t)y * W * 4;
            bool cornerRow = r > 0.f && (((float)y < r) || ((float)y >= H - r));
            for (int x = 0; x < W; x++) {
                const unsigned char* s = srow + x * 4;   // RGBA
                unsigned char*       d = drow + x * 4;   // BGRA, premultiplied
                float cov = 1.0f;
                if (cornerRow && ((float)x < r || (float)x >= W - r)) {
                    float ccx = ((float)x < r) ? r : (W - r);
                    float ccy = ((float)y < r) ? r : (H - r);
                    float dx = (float)x - ccx, dy = (float)y - ccy;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    cov = r + 0.5f - dist;
                    cov = cov < 0.f ? 0.f : (cov > 1.f ? 1.f : cov);
                }
                float a = ppa ? (s[3] / 255.f) * cov : cov;
                if (a >= 0.999f) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255; }
                else {
                    d[0] = (unsigned char)(s[2] * a);
                    d[1] = (unsigned char)(s[1] * a);
                    d[2] = (unsigned char)(s[0] * a);
                    d[3] = (unsigned char)(255.f * a);
                }
            }
        }
        w.context->Unmap(w.staging, 0);

        SIZE  sz  = { w.layW, w.layH };
        POINT src = { 0, 0 };
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)w.constAlpha, AC_SRC_ALPHA };
        UpdateLayeredWindow(w.hwnd, nullptr, nullptr, &sz, w.memDC, &src, 0, &bf, ULW_ALPHA);
    }
    if (w.vsyncFlush)
        DwmFlush();
}

void Shutdown(Window& w) {
    if (w.staging) { w.staging->Release(); w.staging = nullptr; }
    if (w.memDC)   { DeleteDC(w.memDC); w.memDC = nullptr; }
    if (w.dib)     { DeleteObject((HBITMAP)w.dib); w.dib = nullptr; }
    w.dibBits = nullptr;
    ReleaseRTV(w);
    if (w.swapchain) { w.swapchain->Release(); w.swapchain = nullptr; }
    if (w.context)   { w.context->Release();   w.context = nullptr; }
    if (w.device)    { w.device->Release();    w.device = nullptr; }
    if (w.hwnd)      {
        // Stream Hide's capture exclusion goes first: destroying a window that still has it can
        // hang or crash on close.
        SetWindowDisplayAffinity(w.hwnd, 0x00);
        DestroyWindow(w.hwnd);   w.hwnd = nullptr;
    }
    UnregisterClassW(kClass, GetModuleHandleW(nullptr));
}

void BeginDrag(Window& w) {
    ReleaseCapture();
    SendMessageW(w.hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    // The move loop eats the button's release; without this ImGui holds it down and the next click
    // is lost.
    SendMessageW(w.hwnd, WM_LBUTTONUP, 0, 0);
}
void Minimize(Window& w) { ShowWindow(w.hwnd, SW_MINIMIZE); }
void SetWndProcImGuiContext(void* imguiContext) {
    g_wndprocCtx = static_cast<ImGuiContext*>(imguiContext);
}

} // namespace citron::platform
