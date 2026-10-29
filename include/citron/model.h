// What the window shows and saves: the module catalog, each module's settings and their state.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace citron {

enum class Cat { Combat, Movement, Items, Visuals, Settings };

struct CatInfo { Cat cat; const char* key; const char* label; };

struct SliderDef {
    std::string key, label;
    float min = 0.f, max = 1.f, step = 0.01f;
    std::string unit;
    bool range = false;          // two thumbs; the state is key+"_min" and key+"_max"
    float def = 0.f, def2 = 0.f; // default (and the high end of a range)
};

struct CheckDef {
    std::string key, label;
    bool def = false;
};

struct ChoiceDef {
    std::string key, label;
    std::vector<std::string> options;
    int def = 0;
};

// What a module's key does: switch it on and off, act while held, or fire it once per press.
enum class BindKind { Toggle, Hold, Fire, None };

struct ModuleDef {
    std::string key, name, desc;
    Cat cat = Cat::Combat;
    bool hasToggle = true;
    BindKind bind = BindKind::Toggle;
    bool names = false;          // keeps a list of player names (Friends)
    bool hidden = false;         // saved with configs but not shown as a card
    std::vector<SliderDef> sliders;
    std::vector<CheckDef> checks;
    std::vector<ChoiceDef> choices;
    std::string defaultKey;

    ModuleDef& Slider(std::string k, std::string l, float mn, float mx, float st, std::string u, float d) {
        sliders.push_back({std::move(k), std::move(l), mn, mx, st, std::move(u), false, d, d});
        return *this;
    }
    ModuleDef& Range(std::string k, std::string l, float mn, float mx, float st, std::string u, float lo, float hi) {
        sliders.push_back({std::move(k), std::move(l), mn, mx, st, std::move(u), true, lo, hi});
        return *this;
    }
    ModuleDef& Check(std::string k, std::string l, bool d) {
        checks.push_back({std::move(k), std::move(l), d});
        return *this;
    }
    ModuleDef& Choice(std::string k, std::string l, std::vector<std::string> o, int d = 0) {
        choices.push_back({std::move(k), std::move(l), std::move(o), d});
        return *this;
    }
    ModuleDef& Bind(BindKind b) { bind = b; return *this; }
    ModuleDef& Key(std::string k) { defaultKey = std::move(k); return *this; }
    ModuleDef& Names() { names = true; return *this; }
    ModuleDef& NoToggle() { hasToggle = false; return *this; }
    ModuleDef& Hidden() { hidden = true; return *this; }
};

struct ModuleState {
    bool on = false;
    std::string key;             // the bind's label: "V", "Mouse5", "" for none
    std::map<std::string, float> sliders;
    std::map<std::string, bool> checks;
    std::map<std::string, int> choices;
    std::vector<std::string> names;
};

struct AppState {
    Cat category = Cat::Combat;
    std::string expanded;        // the open card
    std::string listening;       // the module whose bind is being captured
    std::vector<std::string> configs = {"Default"};
    std::string activeConfig = "Default";
    unsigned accent = 0;         // 0x00RRGGBB, 0 for the default

    std::vector<ModuleDef> defs;
    std::map<std::string, ModuleState> mods;

    const ModuleDef* Def(const std::string& k) const;
    ModuleState* St(const std::string& k);
    const ModuleState* St(const std::string& k) const;
};

extern const std::vector<CatInfo> kCats;

// The catalog, and every module's state at its defaults.
void BuildModel(AppState& s);
// The state of one module at its defaults.
ModuleState DefaultState(const ModuleDef& d);

std::string FormatValue(float v, float step);
const char* CatLabel(Cat c);

// Bind labels and Win32 virtual keys.
int LabelToVK(const std::string& label);
std::string LabelForVK(int vk);

}  // namespace citron
