// Windows 8.1 for a window without a redirection surface, 10 for a window's own scale.
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include "citron/overlay.h"
#include "citron/log.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"
#include <dcomp.h>
#include <dxgi1_2.h>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace citron::overlay {
namespace {

constexpr wchar_t kClass[] = L"CitronLiteOverlay";
constexpr LONG_PTR kBaseStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

HWND g_hwnd = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain1* g_chain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
IDCompositionDevice* g_comp = nullptr;
IDCompositionTarget* g_target = nullptr;
IDCompositionVisual* g_visual = nullptr;
ImGuiContext* g_ctx = nullptr;
ImGuiContext* g_mainCtx = nullptr;
int g_w = 0, g_h = 0, g_x = 0, g_y = 0;
bool g_shown = false, g_interactive = false, g_captureHidden = false;
float g_scale = 1.f;

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_ctx) {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(g_ctx);
        const LRESULT handled = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
        ImGui::SetCurrentContext(prev);
        if (handled) return handled;
    }
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ReleaseRtv() {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

bool CreateRtv() {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_chain->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return false;
    const bool ok = SUCCEEDED(g_device->CreateRenderTargetView(back, nullptr, &g_rtv));
    back->Release();
    return ok;
}

// A swapchain for composition holds premultiplied alpha, which is what ImGui's blending leaves over a
// clear of nothing at all.
bool EnsureChain(int w, int h) {
    if (g_chain && w == g_w && h == g_h) return true;
    if (g_chain) {
        ReleaseRtv();
        g_context->ClearState();
        g_context->Flush();
        if (FAILED(g_chain->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0))) return false;
        g_w = w;
        g_h = h;
        return CreateRtv();
    }
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    bool ok = SUCCEEDED(g_device->QueryInterface(IID_PPV_ARGS(&dxgi))) && SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
              SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory)));
    if (ok) {
        DXGI_SWAP_CHAIN_DESC1 d{};
        d.Width = (UINT)w;
        d.Height = (UINT)h;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount = 2;
        d.Scaling = DXGI_SCALING_STRETCH;
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        ok = SUCCEEDED(factory->CreateSwapChainForComposition(g_device, &d, nullptr, &g_chain));
    }
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (dxgi) dxgi->Release();
    if (!ok) {
        Log("[overlay] no swapchain for composition");
        return false;
    }
    g_w = w;
    g_h = h;
    if (!CreateRtv() || FAILED(g_visual->SetContent(g_chain)) || FAILED(g_comp->Commit())) {
        Log("[overlay] the swapchain did not take");
        return false;
    }
    return true;
}

void Hide() {
    if (g_shown) ShowWindow(g_hwnd, SW_HIDE);
    g_shown = false;
}

}  // namespace

bool Init(ID3D11Device* device, ID3D11DeviceContext* context, ImFontAtlas* fonts) {
    g_device = device;
    g_context = context;
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW((DWORD)(kBaseStyle | WS_EX_TRANSPARENT), kClass, L"Citron Lite", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) return false;
    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);

    IDXGIDevice* dxgi = nullptr;
    bool ok = SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) && SUCCEEDED(DCompositionCreateDevice(dxgi, IID_PPV_ARGS(&g_comp))) &&
              SUCCEEDED(g_comp->CreateTargetForHwnd(g_hwnd, TRUE, &g_target)) && SUCCEEDED(g_comp->CreateVisual(&g_visual)) &&
              SUCCEEDED(g_target->SetRoot(g_visual));
    if (dxgi) dxgi->Release();
    if (!ok) {
        Log("[overlay] no composition");
        Shutdown();
        return false;
    }

    g_mainCtx = ImGui::GetCurrentContext();
    g_ctx = ImGui::CreateContext(fonts);
    ImGui::SetCurrentContext(g_ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().AntiAliasedLinesUseTex = false;
    ImGui_ImplWin32_Init(g_hwnd);
    // drawn by the main window's renderer, which handles large lists
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::SetCurrentContext(g_mainCtx);
    return true;
}

void Shutdown() {
    if (g_ctx) {
        ImGui::SetCurrentContext(g_ctx);
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(g_ctx);
        ImGui::SetCurrentContext(g_mainCtx);
        g_ctx = nullptr;
    }
    ReleaseRtv();
    if (g_visual) g_visual->Release();
    if (g_target) g_target->Release();
    if (g_comp) g_comp->Release();
    if (g_chain) g_chain->Release();
    g_visual = nullptr;
    g_target = nullptr;
    g_comp = nullptr;
    g_chain = nullptr;
    if (g_hwnd) {
        SetWindowDisplayAffinity(g_hwnd, 0x00);
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
    UnregisterClassW(kClass, GetModuleHandleW(nullptr));
    g_shown = false;
}

void Sync(HWND target, bool interactive) {
    if (!g_hwnd || !g_comp) return;
    RECT rc{};
    POINT at{0, 0};
    if (!target || !GetClientRect(target, &rc) || !ClientToScreen(target, &at) || rc.right <= 0 || rc.bottom <= 0) {
        Hide();
        return;
    }
    if (!EnsureChain(rc.right, rc.bottom)) {
        Hide();
        return;
    }
    if (interactive != g_interactive) {
        g_interactive = interactive;
        SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, kBaseStyle | (interactive ? 0 : WS_EX_TRANSPARENT));
    }
    if (!g_shown || at.x != g_x || at.y != g_y || rc.right != g_w || rc.bottom != g_h) {
        SetWindowPos(g_hwnd, HWND_TOPMOST, at.x, at.y, rc.right, rc.bottom, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        g_x = at.x;
        g_y = at.y;
        g_shown = true;
        g_scale = std::max(0.75f, (float)GetDpiForWindow(target) / 96.f);
    }
}

bool Visible() { return g_shown; }
float Scale() { return g_scale; }

void SetCaptureHidden(bool hidden) {
    if (!g_hwnd || hidden == g_captureHidden) return;
    if (SetWindowDisplayAffinity(g_hwnd, hidden ? 0x11u : 0x00u)) g_captureHidden = hidden;   // WDA_EXCLUDEFROMCAPTURE
}

void BeginFrame() {
    ImGui::SetCurrentContext(g_ctx);
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

// The list goes through the main window's renderer: one font texture, one set of buffers.
void EndFrame() {
    ImGui::Render();
    ImDrawData* list = ImGui::GetDrawData();
    ImGui::SetCurrentContext(g_mainCtx);
    const float nothing[4] = {};
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, nothing);
    ImGui_ImplDX11_RenderDrawData(list);
    g_chain->Present(1, 0);
}

}  // namespace citron::overlay
