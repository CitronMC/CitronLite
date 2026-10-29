// The application: the module state, configs, hotkeys and the panel.
#pragma once
#include "citron/backend.h"
#include "citron/model.h"
#include "imgui.h"
#include <map>
#include <string>
#include <vector>

namespace citron {

struct FrameInput {
    float scale = 1.f;        // device pixels per design pixel
    float fontScale = 1.f;    // the scale the font atlas was built for
    ImTextureID mark = 0;
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
    void Expand(const std::string& key);
    void Preview(const std::string& key, const std::string& bind);   // on, with this bind

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
    float scrollY = 0.f, scrollTarget = 0.f, contentHeight = 0.f;
    float cfgScroll = 0.f, cfgScrollTarget = 0.f;
    double tabSwitchAt = -10.0;
    bool pickerOpen = false;
    float pickerH = 0.f, pickerS = 0.f, pickerV = 0.f;
    std::string renaming;
    std::string confirmDelete;
    float tabsEnd = 0.f, controlsX = 0.f;
    void DrawPanel(const FrameInput& in, FrameOutput& out);
    void DrawHeader(ImDrawList* dl, FrameOutput& out, ImTextureID mark);
    float DrawCard(ImDrawList* dl, const ModuleDef& d, ModuleState& st, float x, float y, float w);
    float CardBodyHeight(const ModuleDef& d, const ModuleState& st) const;
    void DrawCardBody(ImDrawList* dl, const ModuleDef& d, ModuleState& st, float x, float y, float w);
    float DrawFriends(ImDrawList* dl, ModuleState& st, float x, float y, float w);
    float DrawSettings(ImDrawList* dl, float x0, float x1, float y, float colW, float dt);
    float DrawClientCard(ImDrawList* dl, float x, float y, float w);
    float DrawThemeCard(ImDrawList* dl, float x, float y, float w);
    float DrawConfigsCard(ImDrawList* dl, float x, float y, float w, float maxH, float dt);
    float DrawAboutCard(ImDrawList* dl, float x, float y, float w);
    void SyncPicker();
};

}  // namespace citron
