// The window under a mouse: one session through the real exe at each window scale. Every click is a
// real window message at the place the panel draws that control, found by its id, then a look at what
// it changed. A control that does not sit where it is drawn fails here at every scale but 1.
//   ui_tests path\to\CitronLite.exe
#include "citron/config_io.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using citron::ModuleState;

namespace {

int g_failed = 0;
float g_scale = 1.f;

void Check(bool ok, const std::string& what) {
    if (ok) return;
    std::printf("  FAILED at scale %.2f: %s\n", g_scale, what.c_str());
    ++g_failed;
}

std::string Num(float v) { return std::to_string(v); }

class Session {
public:
    Session& operator()(const std::string& line) {
        text += line + "\n";
        return *this;
    }
    Session& Click(const std::string& id) { return (*this)("click @" + id); }
    Session& ClickAt(const std::string& id, float fx, float fy = 0.5f) { return (*this)("click @" + id + " " + Num(fx) + " " + Num(fy)); }
    Session& Down(const std::string& id, float fx, float fy = 0.5f) { return (*this)("down @" + id + " " + Num(fx) + " " + Num(fy)); }
    Session& Move(const std::string& id, float fx, float fy = 0.5f) { return (*this)("move @" + id + " " + Num(fx) + " " + Num(fy)); }
    Session& Key(int vk) { return (*this)("key " + std::to_string(vk)); }
    Session& Tab(const std::string& name) {
        Click("tab:" + name);
        return (*this)("wait 300");
    }
    Session& Open(const std::string& module) {
        Click("row:" + module);
        return (*this)("wait 260");
    }
    std::string text;
};

struct Rect { float x = 0, y = 0, w = 0, h = 0; };
using Rects = std::map<std::string, Rect>;

Rects ReadRects(const std::string& path) {
    Rects out;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        Rect r;
        std::string id;
        row >> r.x >> r.y >> r.w >> r.h;
        std::getline(row >> std::ws, id);
        out[id] = r;
    }
    return out;
}

std::string g_dir;
std::string RectsPath(const char* name) { return g_dir + "ui_rects_" + name + ".txt"; }
Session& Rects_(Session& s, const char* name) { return s("rects " + RectsPath(name)); }

std::string Script() {
    Session s;

    // a light in the list, then the card's own switch
    s.Click("light:triggerbot");
    s.Open("reach").Click("sw:reach");

    // a press on the title bar: the move loop eats its release, and the next click still counts
    s("move 40 30")("down 40 30");
    s.Click("light:hitbox");

    // Aim Assist's card: a slider clicked, a slider dragged, a choice, a checkbox
    s.Open("aimassist");
    s.ClickAt("set:aimassist:strength", 0.75f);
    s.Down("set:aimassist:smoothing", 0.5f).Move("set:aimassist:smoothing", 0.25f)("up");
    s.Click("set:aimassist:aimpart#2");
    s.Click("set:aimassist:onlyLmb");

    // Reach's card: both ends of a range, a value typed
    s.Open("reach");
    s.ClickAt("set:reach:distance", 0.25f);
    s.ClickAt("set:reach:distance", 0.9f);
    s.Click("set:reach:chance#v")("type 42");
    s.Key(VK_RETURN);

    // keys: a letter, a side button, a left click cancels, Escape clears
    s.Open("triggerbot").Click("bind:triggerbot").Key('K');
    s.Open("hitbox").Click("bind:hitbox")("click 640 540 3");
    s.Open("ghosthand").Click("bind:ghosthand")("click 640 540");
    s.Open("clicker").Click("bind:clicker").Key('V');
    s.Click("bind:clicker").Key(VK_ESCAPE);

    // Auto Clicker's two sides: each has its own switch, key and numbers
    s.Click("clicker-side#1")("wait 100");
    s.Click("sw:rightclicker");
    s.Click("bind:rightclicker")("click 640 540 1");
    s.ClickAt("set:rightclicker:cps", 0.95f);
    s.Click("clicker-side#0")("wait 100");
    s.ClickAt("set:clicker:jitter", 0.5f);
    s.Click("set:clicker:breakBlock");
    // the row's light answers for both: lit, it puts both out
    s.Click("sw:clicker");
    s.Click("light:clicker");
    s.Click("light:clicker");

    // the filter: only what is on, and a row switched off stays until the filter changes
    s.Click("filter1")("wait 100");
    Rects_(s, "on");
    s.Click("light:hitbox")("wait 100");
    Rects_(s, "kept");
    s.Click("light:hitbox");
    s.Click("filter2")("wait 100");
    Rects_(s, "bound");
    s.Click("filter0")("wait 100");

    // a list: a dozen names, one out from the end after the wheel, one from the start after the scrollbar
    s.Open("friends");
    for (int i = 0; i < 12; ++i) {
        s.Click("friends:add")("type Player" + std::to_string(i));
        s.Key(VK_RETURN);
    }
    s("move 600 300")("wheel -5")("wait 450");
    s.Click("friends:11:x");
    s.Down("detail-bar", 0.5f, 0.8f).Move("detail-bar", 0.5f, 0.f)("up")("wait 300");
    s.Click("friends:0:x");

    // a long choice steps
    s.Open("teams").Click("set:teams:color#next");

    // the other tabs
    s.Tab("movement");
    s.Click("light:stap");
    s.Open("fastfall").Click("sw:fastfall");
    s.Click("light:autosprint");

    s.Tab("items");
    s.Open("keybindpearl").Click("sw:keybindpearl");
    s.Click("bind:keybindpearl").Key('Q');

    s.Tab("visuals");
    s.Click("light:tpnametags");

    // Settings: nothing here moves, whatever is on it
    s.Tab("settings");
    Rects_(s, "settings");
    s.ClickAt("accent#4", 0.04f, 0.1f);   // the far corner of a swatch's target counts
    s.Click("set:notes");
    s.Click("set:gui").Key('G');
    s.Click("set:stream");
    s.Click("set:task");
    s.Click("set:task:bind").Key('J');
    s.Click("set:taskwin");
    s.Click("set:sd").Key('P');
    s.Click("set:sd:deleteExe");
    return s.text;
}

// Configs on their own: deleting the one in use loads another over everything above.
std::string ConfigScript() {
    Session s;
    s.Tab("settings");
    s.Click("cfg:new")("wait 100")("type Alpha");
    s.Key(VK_RETURN)("wait 100");
    Rects_(s, "named");
    s.Click("cfg:Alpha:pen")("wait 100")("type Beta");
    s.Key(VK_RETURN)("wait 100");
    Rects_(s, "renamed");
    s.Click("cfg:Beta:x")("wait 100");
    s.Click("cfg:Beta:x")("wait 100");
    Rects_(s, "deleted");
    return s.text;
}

// The Click GUI, drawn in the window for the test.
std::string GuiScript() {
    Session s;
    Rects_(s, "gui");
    s.Click("gui:row:reach");
    s.Click("gui:row:zoom");
    s.Click("gui:more:aimassist")("wait 260");
    Rects_(s, "gui-open");
    s.ClickAt("gui:set:aimassist:strength", 0.75f);
    s.Down("gui:set:aimassist:smoothing", 0.5f).Move("gui:set:aimassist:smoothing", 0.25f)("up");
    s.Click("gui:set:aimassist:aimpart#next");
    s.Click("gui:set:aimassist:onlyLmb");
    s.Click("gui:bind:aimassist").Key('K');
    s.Click("gui:more:aimassist")("wait 260");
    s("click @gui:row:velocity 0.5 0.5 1")("wait 260");   // a right click opens it
    s.ClickAt("gui:set:velocity:horizontal", 0.95f);
    s("click @gui:row:velocity 0.5 0.5 1")("wait 260");
    s.Click("gui:more:clicker")("wait 260");
    s.Click("gui:side#1")("wait 200");
    s.Click("gui:bind:rightclicker").Key('N');
    s.ClickAt("gui:set:rightclicker:cps", 0.95f);
    s.Click("gui:row:clicker");
    return s.text;
}

// A second session for the accent picker, which floats over the page.
std::string PickerScript() {
    Session s;
    s.Tab("settings");
    Rects_(s, "closed");
    s.Click("accent:custom")("wait 200");
    Rects_(s, "picking");
    s.ClickAt("accent-pick#sv", 0.3f, 0.2f);
    s.ClickAt("accent-pick#hue", 0.6f);
    s("click 600 500")("wait 100");
    s.Click("set:stream");
    return s.text;
}

std::map<std::string, ModuleState> Run(const std::string& exe, const std::string& script, float scale, unsigned* accent, const char* extra = "") {
    const std::string scriptPath = g_dir + "ui_script.txt", dumpPath = g_dir + "ui_state.txt";
    std::ofstream(scriptPath, std::ios::binary) << script;
    DeleteFileA(dumpPath.c_str());

    char scaleText[16];
    std::snprintf(scaleText, sizeof scaleText, "%.2f", scale);
    std::string cmd = "\"" + exe + "\" --shot --shotscale=" + scaleText + " --script=\"" + scriptPath + "\" --dump=\"" + dumpPath + "\"" + extra;
    STARTUPINFOA si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNOACTIVATE;
    PROCESS_INFORMATION pi{};
    citron::AppState s;
    citron::BuildModel(s);
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::printf("could not start %s\n", exe.c_str());
        std::exit(1);
    }
    if (WaitForSingleObject(pi.hProcess, 120000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        Check(false, "the session hung");
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    std::ifstream in(dumpPath, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    Check(!text.str().empty(), "no state came back");
    citron::cfgio::Deserialize(text.str().c_str(), s.mods, accent);

    std::ifstream gone(dumpPath + ".missing", std::ios::binary);
    for (std::string id; std::getline(gone, id);) Check(false, "no control called " + id + " on screen");
    return s.mods;
}

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

// The Settings page: three rows of two cards, each lined up with the one beside it, at the page's edges.
void CheckSettingsLayout(const Rects& r) {
    auto card = [&](const char* name) {
        const auto it = r.find(std::string("card:") + name);
        Check(it != r.end(), std::string("no card called ") + name);
        return it == r.end() ? Rect{} : it->second;
    };
    const Rect rows[3][2] = {{card("Appearance"), card("In Game")}, {card("Client"), card("Configs")}, {card("Self-Destruct"), card("Citron Lite")}};
    constexpr float kGap = 12.f, kLeft = 16.f, kTop = 56.f, kRight = 904.f, kBottom = 584.f;
    for (int i = 0; i < 3; ++i) {
        const Rect& a = rows[i][0];
        const Rect& b = rows[i][1];
        Check(a.x == kLeft && b.x + b.w == kRight, "the page's edges, row " + std::to_string(i));
        Check(a.w == b.w && a.w == rows[0][0].w, "the columns' width, row " + std::to_string(i));
        Check(b.x - (a.x + a.w) == kGap, "the gutter between the columns, row " + std::to_string(i));
        Check(a.y == b.y && a.h == b.h, "the two cards line up, row " + std::to_string(i));
        if (i) Check(a.y - (rows[i - 1][0].y + rows[i - 1][0].h) == kGap, "the gutter above row " + std::to_string(i));
    }
    Check(rows[0][0].y == kTop, "the page's top");
    Check(rows[2][0].y + rows[2][0].h == kBottom, "the page's last line");
}

// Nothing to miss between two colours.
void CheckAccentTargets(const Rects& r) {
    for (int i = 0; i + 1 < 7; ++i) {
        const auto a = r.find("accent#" + std::to_string(i)), b = r.find("accent#" + std::to_string(i + 1));
        Check(a != r.end() && b != r.end(), "the accent's swatches");
        if (a == r.end() || b == r.end()) return;
        Check(Near(a->second.x + a->second.w, b->second.x, 0.01f), "two colours' targets meet");
        Check(a->second.h >= 30.f, "a colour's target is as tall as the row");
    }
    const auto last = r.find("accent#6"), custom = r.find("accent:custom");
    Check(last != r.end() && custom != r.end() && Near(last->second.x + last->second.w, custom->second.x, 0.01f), "the last one meets the custom colour's");
}

}  // namespace

int main(int argc, char** argv) {
    char full[MAX_PATH] = {};
    GetFullPathNameA(argc > 1 ? argv[1] : "build/CitronLite.exe", MAX_PATH, full, nullptr);
    const std::string exe = full;
    g_dir = exe.substr(0, exe.find_last_of("\\/") + 1);
    for (float scale : {1.f, 0.75f, 1.25f, 1.5f, 2.f}) {
        g_scale = scale;
        unsigned accent = 0;
        auto m = Run(exe, Script(), scale, &accent);

        Check(m["triggerbot"].on, "a light in the list");
        Check(m["reach"].on, "the card's switch");
        Check(m["hitbox"].on, "the first click after moving the window");

        auto& aim = m["aimassist"];
        Check(Near(aim.sliders["strength"], 78.f, 1.f), "a click on a slider: strength " + std::to_string(aim.sliders["strength"]));
        Check(Near(aim.sliders["smoothing"], 25.f, 1.f), "a dragged slider: smoothing " + std::to_string(aim.sliders["smoothing"]));
        Check(aim.choices["aimpart"] == 2, "a segmented choice");
        Check(!aim.checks["onlyLmb"], "a checkbox");

        auto& reach = m["reach"];
        Check(Near(reach.sliders["distance_min"], 3.75f, 0.02f), "a range's low end: " + std::to_string(reach.sliders["distance_min"]));
        Check(Near(reach.sliders["distance_max"], 5.7f, 0.02f), "a range's high end: " + std::to_string(reach.sliders["distance_max"]));
        Check(reach.sliders["chance"] == 42.f, "a typed value: " + std::to_string(reach.sliders["chance"]));

        Check(m["triggerbot"].key == "K", "a letter for a key: '" + m["triggerbot"].key + "'");
        Check(m["hitbox"].key == "Mouse4", "a side button for a key: '" + m["hitbox"].key + "'");
        Check(m["ghosthand"].key.empty(), "a left click cancels: '" + m["ghosthand"].key + "'");
        Check(m["clicker"].key.empty(), "Escape clears: '" + m["clicker"].key + "'");

        auto& left = m["clicker"];
        auto& right = m["rightclicker"];
        Check(right.key == "RMB", "the right side's key: '" + right.key + "'");
        Check(Near(right.sliders["cps_max"], 19.f, 0.6f) && right.sliders["cps_min"] == 8.f, "the right side's numbers: " + std::to_string(right.sliders["cps_max"]));
        Check(left.sliders["cps_max"] == 14.f, "the left side's numbers stay its own: " + std::to_string(left.sliders["cps_max"]));
        Check(Near(left.sliders["jitter"], 10.f, 0.6f), "the left side's jitter: " + std::to_string(left.sliders["jitter"]));
        Check(right.sliders["jitter"] == 0.f, "the right side's jitter stays its own");
        Check(!left.checks["breakBlock"], "a checkbox on the left side");
        Check(left.on && !right.on, "the row's light: out for both, then on for the chosen side");

        const Rects on = ReadRects(RectsPath("on")), kept = ReadRects(RectsPath("kept")), bound = ReadRects(RectsPath("bound"));
        Check(on.count("row:hitbox") && !on.count("row:teams"), "the On filter");
        Check(kept.count("row:hitbox"), "a row switched off stays under the On filter");
        Check(bound.count("row:triggerbot") && !bound.count("row:reach"), "the Bound filter");

        std::vector<std::string> friends;
        for (int i = 1; i <= 10; ++i) friends.push_back("Player" + std::to_string(i));
        Check(m["friends"].names == friends, "a list past the card's end, the wheel and the scrollbar");
        Check(m["teams"].choices["color"] == 1, "a stepped choice");

        Check(m["stap"].on && m["fastfall"].on, "the switches on Movement");
        Check(m["autosprint"].on, "a third module on one tab");
        Check(m["keybindpearl"].on && m["keybindpearl"].key == "Q", "Items");
        Check(m["tpnametags"].on, "Visuals");

        const Rects settings = ReadRects(RectsPath("settings"));
        CheckSettingsLayout(settings);
        CheckAccentTargets(settings);
        Check(accent == 0xFF7A6B, "a swatch, by the corner of its target");
        Check(!m["notifications"].on, "the notifications' switch");
        Check(m["clickgui"].key == "G", "the Click GUI's key: '" + m["clickgui"].key + "'");
        Check(m["streamhide"].on, "a switch in Settings");
        Check(m["taskbarhide"].on && m["taskbarhide"].key == "J", "Taskbar Hide's switch and key: '" + m["taskbarhide"].key + "'");
        Check(m["taskbarhide"].checks["hideWindow"], "Also Hide The Window");
        Check(m["selfdestruct"].key == "P" && m["selfdestruct"].checks["deleteExe"], "Self-Destruct's key and option");

        unsigned ignored = 0;
        Run(exe, ConfigScript(), scale, &ignored);
        Check(ReadRects(RectsPath("named")).count("cfg:Alpha:row"), "a new config named as it comes");
        Check(ReadRects(RectsPath("renamed")).count("cfg:Beta:row"), "a config renamed");
        Check(!ReadRects(RectsPath("deleted")).count("cfg:Beta:row"), "a config deleted");

        auto g = Run(exe, GuiScript(), scale, &ignored, " --gui");
        const Rects cols = ReadRects(RectsPath("gui")), opened = ReadRects(RectsPath("gui-open"));
        for (const char* col : {"combat", "movement", "items", "visuals"}) Check(cols.count(std::string("gui:col:") + col), std::string("a column for ") + col);
        Check(cols.at("gui:col:combat").y == cols.at("gui:col:visuals").y, "the columns stand on one line");
        Check(!cols.count("gui:row:rightclicker"), "the right side has no row of its own");
        Check(opened.at("gui:row:reach").y > cols.at("gui:row:reach").y, "open settings push the rows below down");
        {   // the arrows sit 2 inside the selector's box
            const auto& slider = opened.at("gui:set:aimassist:strength");
            const auto& prev = opened.at("gui:set:aimassist:aimpart#prev");
            const auto& next = opened.at("gui:set:aimassist:aimpart#next");
            Check(Near(prev.x - 2.f, slider.x, 1.f) && Near(next.x + next.w + 2.f, slider.x + slider.w, 1.f), "a stacked choice stands as wide as the sliders");
        }
        Check(g["reach"].on && g["zoom"].on, "a click on a row switches it");
        // a fraction of the strip, and the track stands a knob's width inside it
        Check(Near(g["aimassist"].sliders["strength"], 79.f, 3.f), "a slider in the GUI: " + std::to_string(g["aimassist"].sliders["strength"]));
        Check(Near(g["aimassist"].sliders["smoothing"], 24.f, 3.f), "a dragged slider in the GUI: " + std::to_string(g["aimassist"].sliders["smoothing"]));
        Check(g["aimassist"].choices["aimpart"] == 1, "a choice in the GUI");
        Check(!g["aimassist"].checks["onlyLmb"], "a checkbox in the GUI");
        Check(g["aimassist"].key == "K", "a key in the GUI: '" + g["aimassist"].key + "'");
        Check(g["velocity"].sliders["horizontal_max"] > 180.f, "a right click opens the settings: " + std::to_string(g["velocity"].sliders["horizontal_max"]));
        Check(g["rightclicker"].key == "N", "Auto Clicker's right side in the GUI: '" + g["rightclicker"].key + "'");
        Check(g["rightclicker"].sliders["cps_max"] > 18.f, "the right side's numbers in the GUI");
        Check(g["rightclicker"].on && !g["clicker"].on, "the row works the side that is picked");

        unsigned picked = 0;
        auto after = Run(exe, PickerScript(), scale, &picked);
        Check(picked != 0 && picked != 0xC8F135, "the colour picker");
        Check(after["streamhide"].on, "the page answers again once the picker is put away");
        const Rects closed = ReadRects(RectsPath("closed")), picking = ReadRects(RectsPath("picking"));
        CheckSettingsLayout(picking);
        Check(closed.at("card:Client").y == picking.at("card:Client").y, "the picker moves nothing");
        std::printf("  scale %.2f done\n", scale);
    }
    if (g_failed) {
        std::printf("%d checks failed\n", g_failed);
        return 1;
    }
    std::printf("window session tests passed\n");
    return 0;
}
