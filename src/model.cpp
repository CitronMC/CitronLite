#include "citron/model.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace citron {

const std::vector<CatInfo> kCats = {
    {Cat::Combat, "combat", "Combat"},
    {Cat::Movement, "movement", "Movement"},
    {Cat::Items, "items", "Items"},
    {Cat::Visuals, "visuals", "Visuals"},
    {Cat::Settings, "settings", "Settings"},
};

const ModuleDef* AppState::Def(const std::string& key) const {
    for (const auto& d : defs)
        if (d.key == key) return &d;
    return nullptr;
}

ModuleState* AppState::St(const std::string& key) {
    const auto it = mods.find(key);
    return it == mods.end() ? nullptr : &it->second;
}

const ModuleState* AppState::St(const std::string& key) const {
    const auto it = mods.find(key);
    return it == mods.end() ? nullptr : &it->second;
}

const char* CatLabel(Cat c) {
    for (const auto& info : kCats)
        if (info.cat == c) return info.label;
    return "";
}

std::string FormatValue(float value, float step) {
    int decimals = 0;
    if (step < 1.f) {
        char b[32];
        std::snprintf(b, sizeof b, "%g", step);
        if (const char* dot = std::strchr(b, '.')) decimals = (int)std::strlen(dot + 1);
        if (!decimals) decimals = 2;
    }
    char out[32];
    std::snprintf(out, sizeof out, "%.*f", decimals, value);
    return out;
}

ModuleState DefaultState(const ModuleDef& d) {
    ModuleState st;
    st.key = d.defaultKey;
    for (const auto& s : d.sliders) {
        if (s.range) {
            st.sliders[s.key + "_min"] = s.def;
            st.sliders[s.key + "_max"] = s.def2;
        } else {
            st.sliders[s.key] = s.def;
        }
    }
    for (const auto& c : d.checks) st.checks[c.key] = c.def;
    for (const auto& c : d.choices) st.choices[c.key] = c.def;
    return st;
}

void BuildModel(AppState& s) {
    const char* deg = "\xC2\xB0";
    const std::vector<std::string> teams = {"Auto", "Red", "Blue", "Green", "Yellow", "Aqua", "Pink",
                                            "Purple", "Orange", "White", "Gray", "Black", "Gold"};
    auto def = [&](const char* key, const char* name, const char* desc, Cat cat) -> ModuleDef& {
        ModuleDef d;
        d.key = key;
        d.name = name;
        d.desc = desc;
        d.cat = cat;
        s.defs.push_back(std::move(d));
        return s.defs.back();
    };
    s.defs.clear();
    s.defs.reserve(40);

    // Combat
    def("aimassist", "Aim Assist", "Eases your aim onto the player nearest your crosshair", Cat::Combat)
        .Slider("strength", "Strength", 10, 100, 1, "%", 55)
        .Slider("smoothing", "Smoothing", 0, 100, 1, "%", 40)
        .Slider("distance", "Distance", 2, 7, 0.01f, " blocks", 4.5f)
        .Slider("fov", "FOV Cone", 5, 180, 1, deg, 45)
        .Choice("aimpart", "Aim At", {"Closest", "Head", "Body"})
        .Check("onlyLmb", "Only While Attacking", true)
        .Check("playersOnly", "Players Only", true)
        .Check("horizontalOnly", "Horizontal Only", false)
        .Check("matchDirection", "Follow Mouse Direction", false)
        .Check("onlyWeapon", "Only With A Weapon", false);
    def("triggerbot", "Trigger Bot", "Attacks whoever is under your crosshair in reach", Cat::Combat)
        .Range("cps", "CPS", 1, 20, 0.5f, "", 8, 12)
        .Range("delay", "Reaction Delay", 0, 300, 5, " ms", 0, 0)
        .Check("playersOnly", "Players Only", true)
        .Check("onlyWeapon", "Only With A Weapon", false);
    def("reach", "Reach", "Extends how far your hits go", Cat::Combat)
        .Range("distance", "Distance", 3, 6, 0.01f, " blocks", 4.5f, 6.0f)
        .Slider("chance", "Chance", 0, 100, 1, "%", 100);
    def("hitbox", "Hitbox", "Widens other players' hitboxes", Cat::Combat)
        .Slider("width", "Width", 0, 1, 0.01f, " blocks", 0.3f)
        .Slider("height", "Height", 0, 1, 0.01f, " blocks", 0.3f)
        .Check("shadow", "Shadow Spoof", true);
    def("ghosthand", "Ghost Hand", "Hits players through blocks", Cat::Combat);
    def("clicker", "Auto Clicker", "Clicks for you while you hold attack", Cat::Combat)
        .Range("cps", "CPS", 1, 20, 0.5f, "", 10, 14)
        .Slider("jitter", "Jitter", 0, 20, 0.5f, " px", 3)
        .Check("breakBlock", "Break Blocks", true)
        .Check("avoidMenus", "Avoid Menus", true);
    def("rightclicker", "Right Clicker", "Clicks for you while you hold use", Cat::Combat)
        .Range("cps", "CPS", 1, 20, 0.5f, "", 8, 12)
        .Slider("jitter", "Jitter", 0, 20, 0.5f, " px", 0);
    def("teams", "Teams", "Leaves players on your team alone", Cat::Combat)
        .Choice("color", "Team Color", teams);
    def("friends", "Friends", "Leaves the players on your list alone", Cat::Combat)
        .Names()
        .Check("middleClick", "Middle Click Adds", true);

    // Movement
    def("velocity", "Velocity", "Scales the knockback you take", Cat::Movement)
        .Range("horizontal", "Horizontal", 0, 200, 1, "%", 50, 70)
        .Range("vertical", "Vertical", 0, 200, 1, "%", 70, 90)
        .Slider("chance", "Chance", 0, 100, 1, "%", 100);
    def("jumpreset", "Jump Reset", "Jumps the moment a player hits you", Cat::Movement)
        .Range("delay", "Delay", 0, 750, 5, " ms", 30, 80)
        .Slider("chance", "Chance", 0, 100, 1, "%", 100)
        .Slider("fov", "Attacker FOV", 30, 360, 5, deg, 360)
        .Check("simple", "Instant", false)
        .Check("hitTrade", "Hit Trade Pattern", false);
    def("stap", "S-Tap", "Backs off for a moment after each hit", Cat::Movement)
        .Range("hold", "Tap Time", 20, 250, 1, " ms", 60, 110)
        .Range("range", "Hit Distance", 0, 6, 0.1f, " blocks", 0, 3)
        .Slider("chance", "Chance", 0, 100, 1, "%", 100)
        .Check("sprintOnly", "Only When Sprinting", false);
    def("snaptap", "SnapTap", "The newest movement key wins", Cat::Movement)
        .Choice("keys", "Keys", {"Both", "Forward & Back", "Left & Right"});
    def("autosprint", "Auto Sprint", "Keeps you sprinting", Cat::Movement)
        .Check("strafe", "Sprint While Strafing", true);
    def("nojumpdelay", "No Jump Delay", "Removes the wait between jumps", Cat::Movement);
    def("airaccel", "Air Accel", "Changes how fast you speed up in the air", Cat::Movement)
        .Slider("multiplier", "Speed", 10, 400, 5, "%", 100);
    def("fastfall", "Fast Fall", "Brings you down a drop faster", Cat::Movement)
        .Range("speed", "Fall Speed", 0, 3.92f, 0.001f, " blocks", 1.4f, 1.6f)
        .Range("timer", "Timer Speed", 1, 5, 0.001f, "x", 1, 1)
        .Check("afterJump", "After Jumps", false);
    def("timer", "Timer", "Changes how fast the game ticks", Cat::Movement)
        .Slider("ticks", "Speed", 1, 40, 0.001f, " ticks", 20);

    // Items
    def("keybindpearl", "Keybind Pearl", "Throws a pearl from your hotbar on its key", Cat::Items).Bind(BindKind::Fire);
    def("keybindsnowball", "Keybind Snowball", "Throws a snowball from your hotbar on its key", Cat::Items)
        .Bind(BindKind::Fire)
        .Check("eggs", "Eggs Too", false);
    def("keybindpot", "Keybind Pot", "Throws a healing potion from your hotbar on its key", Cat::Items).Bind(BindKind::Fire);
    def("nousedelay", "No Use Delay", "Uses items right after a hit", Cat::Items);

    // Visuals
    def("zoom", "Zoom", "Zooms in while you hold its key", Cat::Visuals)
        .Bind(BindKind::Hold)
        .Slider("fov", "Zoom FOV", 5, 110, 1, deg, 30)
        .Slider("step", "Scroll Step", 1, 30, 1, deg, 10)
        .Check("smooth", "Smooth Animation", true)
        .Check("scroll", "Scroll To Adjust", true)
        .Check("invertScroll", "Invert Scroll", false);
    def("customfov", "Custom FOV", "Sets your field of view past the game's limits", Cat::Visuals)
        .Slider("fov", "Field Of View", 10, 170, 1, deg, 120);
    def("fullbright", "Fullbright", "Lights up the whole world", Cat::Visuals);
    def("tpnametags", "Third Person Nametags", "Shows your own nametag in third person", Cat::Visuals);

    // Settings
    def("streamhide", "Stream Hide", "Keeps this window out of screen capture", Cat::Settings);
    def("taskbarhide", "Taskbar Hide", "Takes this window off the taskbar", Cat::Settings)
        .Key("Insert")
        .Check("hideWindow", "Also Hide The Window", false);
    def("selfdestruct", "Self-Destruct", "Puts the game back and closes Citron Lite", Cat::Settings)
        .Bind(BindKind::Fire)
        .NoToggle()
        .Check("closeGame", "Also Close Minecraft", false)
        .Check("deleteExe", "Delete CitronLite.exe", false)
        .Check("wipeData", "Wipe Citron Lite's Data", false);

    s.mods.clear();
    for (const auto& d : s.defs) s.mods[d.key] = DefaultState(d);
}

int LabelToVK(const std::string& label) {
    if (label.empty() || label == "NONE") return 0;
    static const struct { const char* name; int vk; } kNamed[] = {
        {"LMB", VK_LBUTTON}, {"RMB", VK_RBUTTON}, {"MMB", VK_MBUTTON}, {"Mouse4", VK_XBUTTON1}, {"Mouse5", VK_XBUTTON2},
        {"Space", VK_SPACE}, {"Enter", VK_RETURN}, {"Tab", VK_TAB}, {"Ctrl", VK_CONTROL}, {"Shift", VK_SHIFT},
        {"Alt", VK_MENU}, {"RShift", VK_RSHIFT}, {"RCtrl", VK_RCONTROL}, {"Insert", VK_INSERT}, {"Del", VK_DELETE},
        {"Back", VK_BACK}, {"ESC", VK_ESCAPE}, {"Up", VK_UP}, {"Down", VK_DOWN}, {"Left", VK_LEFT},
        {"Right", VK_RIGHT}, {"Home", VK_HOME}, {"End", VK_END}, {"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT},
        {"CapsLock", VK_CAPITAL}, {"ScrollLock", VK_SCROLL}, {"NumLock", VK_NUMLOCK}, {"Pause", VK_PAUSE},
        {"PrintScreen", VK_SNAPSHOT}, {"Menu", VK_APPS}, {"NumAdd", VK_ADD}, {"NumSubtract", VK_SUBTRACT},
        {"NumMultiply", VK_MULTIPLY}, {"NumDivide", VK_DIVIDE}, {"NumDecimal", VK_DECIMAL},
        {"Apostrophe", VK_OEM_7}, {"Comma", VK_OEM_COMMA}, {"Minus", VK_OEM_MINUS}, {"Period", VK_OEM_PERIOD},
        {"Slash", VK_OEM_2}, {"Semicolon", VK_OEM_1}, {"Equal", VK_OEM_PLUS}, {"LeftBracket", VK_OEM_4},
        {"Backslash", VK_OEM_5}, {"RightBracket", VK_OEM_6}, {"GraveAccent", VK_OEM_3},
    };
    for (const auto& k : kNamed)
        if (label == k.name) return k.vk;
    if (label.size() == 4 && label.rfind("Num", 0) == 0 && label[3] >= '0' && label[3] <= '9') return VK_NUMPAD0 + (label[3] - '0');
    if (label.size() == 1) {
        const char c = label[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (unsigned char)c;
    }
    if (label[0] == 'F' && label.size() <= 3) {
        const int n = std::atoi(label.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    return 0;
}

std::string LabelForVK(int vk) {
    for (int c = 'A'; c <= 'Z'; ++c)
        if (vk == c) return std::string(1, (char)c);
    for (int c = '0'; c <= '9'; ++c)
        if (vk == c) return std::string(1, (char)c);
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Num" + std::to_string(vk - VK_NUMPAD0);
    switch (vk) {
    case VK_LBUTTON: return "LMB";
    case VK_RBUTTON: return "RMB";
    case VK_MBUTTON: return "MMB";
    case VK_XBUTTON1: return "Mouse4";
    case VK_XBUTTON2: return "Mouse5";
    case VK_SPACE: return "Space";
    case VK_RETURN: return "Enter";
    case VK_TAB: return "Tab";
    case VK_LCONTROL: case VK_CONTROL: return "Ctrl";
    case VK_RCONTROL: return "RCtrl";
    case VK_LSHIFT: case VK_SHIFT: return "Shift";
    case VK_RSHIFT: return "RShift";
    case VK_LMENU: case VK_RMENU: case VK_MENU: return "Alt";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Del";
    case VK_BACK: return "Back";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_CAPITAL: return "CapsLock";
    case VK_SCROLL: return "ScrollLock";
    case VK_NUMLOCK: return "NumLock";
    case VK_PAUSE: return "Pause";
    case VK_SNAPSHOT: return "PrintScreen";
    case VK_APPS: return "Menu";
    case VK_ADD: return "NumAdd";
    case VK_SUBTRACT: return "NumSubtract";
    case VK_MULTIPLY: return "NumMultiply";
    case VK_DIVIDE: return "NumDivide";
    case VK_DECIMAL: return "NumDecimal";
    case VK_OEM_7: return "Apostrophe";
    case VK_OEM_COMMA: return "Comma";
    case VK_OEM_MINUS: return "Minus";
    case VK_OEM_PERIOD: return "Period";
    case VK_OEM_2: return "Slash";
    case VK_OEM_1: return "Semicolon";
    case VK_OEM_PLUS: return "Equal";
    case VK_OEM_4: return "LeftBracket";
    case VK_OEM_5: return "Backslash";
    case VK_OEM_6: return "RightBracket";
    case VK_OEM_3: return "GraveAccent";
    default: return "";
    }
}

}  // namespace citron
