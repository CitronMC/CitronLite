// The application: the module state, configs, hotkeys and the panel.
#pragma once
#include "citron/backend.h"
#include "citron/model.h"
#include "imgui.h"
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
    void Frame(const FrameInput& in, FrameOutput& out);
    void SetHwnd(void* hwnd);
    // The panel's drag handle, in design pixels from its top left.
    bool IsDragHandle(float x, float y) const;

    // headless captures
    void SelectTab(Cat c);
    void Expand(const std::string& key);                             // opens this module
    void Preview(const std::string& key, const std::string& bind);   // on, with this bind
    void SaveTo(const std::string& path) const;

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
    void PollHotkeys();
    void SelfDestruct();
    void ApplyFriendEdits();

    // the panel (panel.cpp)
    void DrawPanel(const FrameInput& in, FrameOutput& out);
    void DrawTopBar(const FrameInput& in, FrameOutput& out);
    float DrawStatus(float x1, float cy);
    void DrawSettings(const ui::R& page);
    void DrawConfigs(const ui::R& card);
};

}  // namespace citron
