// The application: the module state, configs, hotkeys and the panel.
#pragma once
#include "citron/backend.h"
#include "citron/model.h"
#include "imgui.h"
#include "ui/ingame.h"
#include <map>
#include <string>
#include <vector>

namespace citron {

namespace ui { struct R; }

struct FrameInput {
    float scale = 1.f;            // device pixels per design pixel
    float fontScale = 1.f;        // the scale the font atlas was built for
    ImTextureID letters = 0;      // the wordmark's two masks, tinted when drawn
    ImTextureID leaves = 0;
    float logoAspect = 3.f;       // width / height
    ImTextureID icons = 0;
    bool foreground = true;
};

struct FrameOutput {
    bool wantClose = false;
    bool wantMinimize = false;
    bool wantDrag = false;
};

class App {
public:
    void Init(bool headless);
    void Shutdown();
    // Hotkeys, the Click GUI, notifications and the backend's copy of the state: every pass of the loop,
    // whether or not this window is drawn.
    void Tick();
    void Frame(const FrameInput& in, FrameOutput& out);
    void SetHwnd(void* hwnd);
    // The panel's drag handle, in design pixels from its top left.
    bool IsDragHandle(float x, float y) const;

    // headless captures
    void SelectTab(Cat c);
    void Expand(const std::string& key);                             // opens this module
    void Preview(const std::string& key, const std::string& bind);   // on, with this bind
    void SaveTo(const std::string& path) const;
    void PreviewGui() { previewGui = true; }                         // the panel's window shows the Click GUI

    // over the game
    bool OverlayWanted() const { return gui != Gui::Closed || !toasts.empty(); }
    bool OverlayInteractive() const { return gui == Gui::Open; }
    void* GameWindow() const;                                        // null unless the game is in front
    bool StreamHidden() const;
    void OverlayFrame(const FrameInput& in);

private:
    AppState s;
    Backend backend;
    bool headless = false;
    void* hwnd = nullptr;

    // configs
    std::string autosaveBaseline, autosaveSeen;
    unsigned long autosaveDirtySince = 0;
    void LoadConfigs();
    bool LoadConfig(const std::string& name);
    void SaveActive();
    void Autosave();
    void NewConfig();
    bool RenameConfig(const std::string& from, const std::string& to);
    void DeleteConfig(const std::string& name);
    void ImportConfig();
    void ExportConfig();
    void RememberActive();

    // hotkeys
    std::map<std::string, std::pair<int, uint32_t>> seenPresses;
    bool justBound = false;
    uint32_t PressesOf(const std::string& id, int vk);
    void PollHotkeys();
    void SelfDestruct();
    void ApplyFriendEdits();

    // the Click GUI
    enum class Gui { Closed, Opening, Open };
    Gui gui = Gui::Closed;
    unsigned long long guiAt = 0;
    int guiScreen = 0;      // what freed the pointer: 0 the game let go of it, 1 its chat, 2 its pause screen
    bool previewGui = false;
    uint32_t guiPressSeen = 0;
    void ClickGuiTick();
    void CloseGui(bool backToGame);

    // notifications
    std::vector<Toast> toasts;
    std::map<std::string, bool> seenOn;
    void NoteToggles();
    void PushToast(const std::string& title, const std::string& state, bool lit);

    // the panel (panel.cpp)
    void DrawPanel(const FrameInput& in, FrameOutput& out);
    void DrawTopBar(const FrameInput& in, FrameOutput& out);
    float DrawStatus(float x1, float cy);
    void DrawSettings(const ui::R& page);
    void DrawConfigs(const ui::R& card);
};

}  // namespace citron
