#include "citron/app.h"
#include "citron/config_io.h"
#include "citron/font_data.h"
#include "citron/log.h"
#include "citron/logo.h"
#include "citron/platform.h"
#include "citron/ui_script.h"
#include "citron/updater.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"
#include "ui/draw.h"
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

using namespace citron;

namespace {

struct InstanceLease {
    HANDLE handle = nullptr;
    ~InstanceLease() {
        if (handle) {
            ReleaseMutex(handle);
            CloseHandle(handle);
        }
    }
};

// One copy at a time, whichever folder it runs from; a second launch brings the first forward.
bool AcquireSingleInstance(InstanceLease& lease) {
    HANDLE h = CreateMutexW(nullptr, TRUE, L"Local\\CitronLite.SingleInstance.1A70C9E4");
    if (!h) return false;
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        lease.handle = h;
        return true;
    }
    platform::ActivateExistingInstance(1500);
    CloseHandle(h);
    return false;
}

const char* Arg(const char* name) {
    const char* at = std::strstr(GetCommandLineA(), name);
    return at ? at + std::strlen(name) : nullptr;
}

std::string ArgWord(const char* name) {
    const char* v = Arg(name);
    if (!v) return {};
    const char stop = *v == '"' ? '"' : ' ';
    if (*v == '"') ++v;
    std::string out;
    while (*v && *v != stop) out += *v++;
    return out;
}

void WriteCrashRecord(const char* what, unsigned long code, unsigned long long rva, const wchar_t* module) {
    if (FILE* f = std::fopen((cfgio::Dir() + "citron-lite-crash.txt").c_str(), "a")) {
        std::fprintf(f, "[crash] %s code=0x%08lX exe+0x%llX in %ls\n", what, code, rva, module);
        std::fclose(f);
    }
}

LONG WINAPI CrashLogger(EXCEPTION_POINTERS* ep) {
    const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    const auto* rec = ep ? ep->ExceptionRecord : nullptr;
    const uintptr_t addr = rec ? (uintptr_t)rec->ExceptionAddress : 0;
    HMODULE hm = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &hm);
    wchar_t path[MAX_PATH] = L"?";
    if (hm) GetModuleFileNameW(hm, path, MAX_PATH);
    WriteCrashRecord("fault", (unsigned long)(rec ? rec->ExceptionCode : 0), (unsigned long long)(addr - base), path);
    return EXCEPTION_EXECUTE_HANDLER;
}

void SaveBackbufferBMP(platform::Window& win, const char* path) {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(win.swapchain->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return;
    D3D11_TEXTURE2D_DESC d;
    back->GetDesc(&d);
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ID3D11Texture2D* stg = nullptr;
    if (FAILED(win.device->CreateTexture2D(&sd, nullptr, &stg)) || !stg) {
        back->Release();
        return;
    }
    win.context->CopyResource(stg, back);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(win.context->Map(stg, 0, D3D11_MAP_READ, 0, &m))) {
        const int W = (int)d.Width, H = (int)d.Height;
        std::vector<unsigned char> px((size_t)W * H * 4);
        for (int y = 0; y < H; ++y) {
            const unsigned char* row = (const unsigned char*)m.pData + (size_t)y * m.RowPitch;
            for (int x = 0; x < W; ++x) {
                unsigned char* o = &px[((size_t)y * W + x) * 4];
                o[0] = row[x * 4 + 2];
                o[1] = row[x * 4 + 1];
                o[2] = row[x * 4 + 0];
                o[3] = row[x * 4 + 3];
            }
        }
        win.context->Unmap(stg, 0);
        if (FILE* f = std::fopen(path, "wb")) {
            const int dataSize = W * H * 4, fileSize = 14 + 40 + dataSize, offset = 54;
            unsigned char fh[14] = {'B', 'M'};
            std::memcpy(fh + 2, &fileSize, 4);
            std::memcpy(fh + 10, &offset, 4);
            unsigned char ih[40] = {};
            int v = 40;
            std::memcpy(ih, &v, 4);
            std::memcpy(ih + 4, &W, 4);
            v = -H;
            std::memcpy(ih + 8, &v, 4);
            ih[12] = 1;
            ih[14] = 32;
            std::memcpy(ih + 20, &dataSize, 4);
            std::fwrite(fh, 1, 14, f);
            std::fwrite(ih, 1, 40, f);
            std::fwrite(px.data(), 1, (size_t)dataSize, f);
            std::fclose(f);
        }
    }
    stg->Release();
    back->Release();
}

float g_fontScale = 0.f;

void BuildFonts(float scale) {
    const ui::FontFile files[(int)ui::Face::Count] = {
        {fontdata::kHankenRegular, (int)sizeof fontdata::kHankenRegular},
        {fontdata::kHankenMedium, (int)sizeof fontdata::kHankenMedium},
        {fontdata::kHankenSemiBold, (int)sizeof fontdata::kHankenSemiBold},
        {fontdata::kSchibstedSemiBold, (int)sizeof fontdata::kSchibstedSemiBold},
        {fontdata::kSchibstedBold, (int)sizeof fontdata::kSchibstedBold},
        {fontdata::kMonoMedium, (int)sizeof fontdata::kMonoMedium},
    };
    ui::BuildFonts(ImGui::GetIO().Fonts, files, scale);
    g_fontScale = scale;
}

void BeginScaledFrame(platform::Window& win) {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    const float scale = win.scale > 0.f ? win.scale : 1.f;
    io.DisplaySize = ImVec2(win.width > 0 ? (float)win.width : ui::kPanelW * scale, win.height > 0 ? (float)win.height : ui::kPanelH * scale);
    io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
    ImGui::NewFrame();
}

struct Loop {
    platform::Window* win = nullptr;
    App* app = nullptr;
};

void RenderOnce(platform::Window& w, App& app, bool foreground, const std::string& capture = {}) {
    BeginScaledFrame(w);
    FrameInput in;
    in.scale = w.scale > 0.f ? w.scale : 1.f;
    in.fontScale = g_fontScale;
    in.letters = LogoLetters();
    in.leaves = LogoLeaves();
    in.logoAspect = LogoAspect();
    in.icons = IconAtlas();
    in.foreground = foreground;
    FrameOutput out;
    app.Frame(in, out);
    ImGui::Render();
    const float clear[4] = {0x12 / 255.f, 0x14 / 255.f, 0x11 / 255.f, 1.f};
    platform::BeginRender(w, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (!capture.empty()) SaveBackbufferBMP(w, capture.c_str());
    platform::EndRender(w);
    if (out.wantClose) PostQuitMessage(0);
    if (out.wantMinimize) platform::Minimize(w);
    if (out.wantDrag) platform::BeginDrag(w);
}

void RenderDuringResize(platform::Window& w, void* ctx) {
    auto* l = static_cast<Loop*>(ctx);
    RenderOnce(w, *l->app, true);
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    citron::LogPath() = cfgio::Dir() + "citron-lite.log";
    citron::LogEnabled() = Arg("--debug") || GetFileAttributesA((cfgio::Dir() + "debug.txt").c_str()) != INVALID_FILE_ATTRIBUTES;

    // An update restarts into the new build, which waits here for the old one to finish putting the
    // game back and let go of the single-instance lock.
    if (const char* pidArg = Arg("--await-pid="))
        if (const DWORD pid = (DWORD)std::strtoul(pidArg, nullptr, 10))
            if (HANDLE previous = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
                WaitForSingleObject(previous, 30000);
                CloseHandle(previous);
            }

    const bool shot = Arg("--shot") != nullptr;
    InstanceLease lease;
    if (!shot && !AcquireSingleInstance(lease)) return 0;
    SetUnhandledExceptionFilter(CrashLogger);
    std::set_terminate([] {
        WriteCrashRecord("std::terminate", 0, 0, L"-");
        std::abort();
    });
    ImGui_ImplWin32_EnableDpiAwareness();

    float scale = 1.f;
    if (!shot) scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    else if (const char* s = Arg("--shotscale=")) scale = std::max(0.5f, (float)std::atof(s));

    platform::Window win;
    if (!platform::Init(win, (int)ui::kPanelW, (int)ui::kPanelH, L"Citron Lite", ui::kPanelRadius, scale, shot)) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    platform::SetWndProcImGuiContext(ImGui::GetCurrentContext());
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().AntiAliasedLinesUseTex = false;
    BuildFonts(win.scale);
    ImGui_ImplWin32_Init(win.hwnd);
    ImGui_ImplDX11_Init(win.device, win.context);
    LoadLogo(win.device, win.context);

    App app;
    app.Init(shot);
    app.SetHwnd(win.hwnd);
    Loop loop{&win, &app};
    win.frameCtx = &loop;
    win.onFrame = RenderDuringResize;

    // headless captures: --shot=out.bmp [--tab=combat] [--expand=aimassist] [--on=reach,zoom:C]
    // [--shotframe=N] [--shotscale=2]; with --script=steps.txt [--dump=state.txt] a session for the
    // UI tests runs instead
    const std::string tab = ArgWord("--tab=");
    for (const CatInfo& c : kCats)
        if (tab == c.key) app.SelectTab(c.cat);
    if (const std::string key = ArgWord("--expand="); !key.empty()) app.Expand(key);
    for (std::string on = ArgWord("--on="); !on.empty();) {
        const size_t comma = on.find(',');
        const std::string item = on.substr(0, comma);
        const size_t colon = item.find(':');
        app.Preview(item.substr(0, colon), colon == std::string::npos ? std::string() : item.substr(colon + 1));
        on = comma == std::string::npos ? std::string() : on.substr(comma + 1);
    }
    std::string shotPath = ArgWord("--shot=");
    if (shot && shotPath.empty()) shotPath = "citron-lite-shot.bmp";
    const int shotFrame = Arg("--shotframe=") ? std::max(2, std::atoi(Arg("--shotframe="))) : 40;
    int frame = 0;
    UiScript script;
    const bool scripted = shot && script.Load(ArgWord("--script="));
    if (scripted) ui::RecordItems(true);

    bool restarted = false;
    ULONGLONG nextRestartCheck = 0;
    while (platform::Pump(win)) {
        // An installed update restarts by itself, but never while you are in the game.
        if (!shot && !restarted && updater::Enabled() && GetTickCount64() >= nextRestartCheck) {
            nextRestartCheck = GetTickCount64() + 500;
            if (updater::Get().stage == updater::Stage::Ready && !platform::ForegroundIsBedrock(0)) {
                restarted = true;
                if (updater::RestartToApply()) PostQuitMessage(0);
            }
        }
        const bool minimized = IsIconic(win.hwnd) != FALSE;
        const bool background = !shot && GetForegroundWindow() != win.hwnd;
        // Behind the game this window costs the game input latency: it draws a few times a second
        // and never waits on the compositor.
        if (minimized) Sleep(60);
        else if (background) Sleep(40);
        win.vsyncFlush = !background && !scripted;

        if (!win.inSizeMove && std::fabs(win.scale - g_fontScale) > 0.02f) {
            BuildFonts(win.scale);
            ImGui_ImplDX11_InvalidateDeviceObjects();
            ImGui_ImplDX11_CreateDeviceObjects();
        }
        if (minimized) continue;
        std::string capture;
        if (scripted && !script.Step(win, capture)) break;
        const bool last = shot && !scripted && ++frame == shotFrame;
        RenderOnce(win, app, !background, last ? shotPath : capture);
        if (last) break;
    }

    if (const std::string dump = ArgWord("--dump="); !dump.empty()) {
        app.SaveTo(dump);
        if (FILE* f = std::fopen((dump + ".missing").c_str(), "wb")) {
            for (const std::string& id : script.Missing()) std::fprintf(f, "%s\n", id.c_str());
            std::fclose(f);
        }
    }
    app.Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    platform::Shutdown(win);
    return 0;
}
