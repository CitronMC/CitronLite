// The backend: finds Minecraft, resolves the game sites for its build and runs the modules on a
// worker thread. The window talks to it through snapshots of the module state.
#pragma once
#include "citron/model.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace citron {

struct BackendStatus {
    enum class Kind { Searching, Unsupported, Attached, Partial };
    Kind kind = Kind::Searching;
    std::string version;                 // the game's, "1.26.60.5"
    std::string detail;                  // one line for when something is missing
    std::vector<std::string> missing;    // modules this build cannot run
    bool inWorld = false;
    int enabledModules = 0;
    std::vector<std::string> friendEdits;   // names middle clicked in game, for the window to toggle
};

struct ModuleSnapshot {
    bool on = false;
    std::string key;
    int bind = 0;
    std::map<std::string, float> sliders;
    std::map<std::string, bool> checks;
    std::map<std::string, int> choices;
    std::vector<std::string> names;
    bool operator==(const ModuleSnapshot&) const = default;
};

class Backend {
public:
    Backend();
    ~Backend();
    void Init();
    void Shutdown();
    void Sync(const AppState& state);
    void SetHwnd(void* hwnd);
    BackendStatus Status() const;
    // The names middle clicked since the last call; each one is added or, when listed, removed.
    std::vector<std::string> TakeFriendEdits();
    // Presses a key in the game, your chat binding or another. False when no game is attached.
    bool PressChat();
    bool PressKey(int vk);
    void SetGuiOpen(bool open);
    unsigned long GamePid() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Physical down state and press count of a key or mouse button, from the low-level hooks. Keys and
// clicks Citron Lite sends itself never count.
bool PhysKeyDown(int vk);
uint32_t PhysKeyEdges(int vk);
// While the keyboard is taken, physical key presses stop at the hook instead of reaching whatever is in
// front. They still count here.
void SetKeysTaken(bool on);
// How many keys and buttons have gone down, and which was last.
uint32_t KeyPressCount();
int LastKeyPress();

}  // namespace citron
