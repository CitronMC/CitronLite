// The window under a mouse: one session through the real exe at each window scale, every click a
// real window message at the place the control is drawn, then a look at what it changed. A control
// that does not sit where it is drawn fails here at every scale but 1.
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

using citron::ModuleState;

namespace {

int g_failed = 0;
float g_scale = 1.f;

void Check(bool ok, const std::string& what) {
    if (ok) return;
    std::printf("  FAILED at scale %.2f: %s\n", g_scale, what.c_str());
    ++g_failed;
}

// Where the panel draws things, in design pixels.
constexpr float kLeft = 18.f, kRight = 436.f, kCardW = 406.f, kRowH = 76.f, kTop = 79.f;
float CardY(int row) { return kTop + (float)row * kRowH; }
float SwitchX(float col) { return col + kCardW - 16.f - 15.f; }
float BindX(float col, float width = 48.f) { return col + kCardW - 16.f - 30.f - 12.f - width * 0.5f; }
float RowMid(int row) { return CardY(row) + 32.f; }
float StripX(float fraction) { return 34.f + 374.f * fraction; }

class Session {
public:
    Session& operator()(const std::string& line) {
        text += line + "\n";
        return *this;
    }
    Session& Click(float x, float y, int button = 0) {
        return (*this)("click " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(button));
    }
    Session& Down(float x, float y) { return (*this)("down " + std::to_string(x) + " " + std::to_string(y)); }
    Session& Move(float x, float y) { return (*this)("move " + std::to_string(x) + " " + std::to_string(y)); }
    Session& Key(int vk) { return (*this)("key " + std::to_string(vk)); }
    Session& Tab(float x) {
        Click(x, 30.f);
        return (*this)("wait 260");
    }
    Session& Fold(float x, float y) {
        Click(x, y);
        return (*this)("wait 260");
    }
    std::string text;
};

std::string Script() {
    Session s;
    const float aimHead = RowMid(0), reachHead = RowMid(1);

    // switches in both columns
    s.Click(SwitchX(kLeft), RowMid(0));
    s.Click(SwitchX(kRight), RowMid(0));

    // a press on the title bar: the move loop eats its release, and the next click still counts
    s.Move(100.f, 30.f).Down(100.f, 30.f);
    s.Click(SwitchX(kRight), RowMid(1));

    // Aim Assist's card: a slider clicked, a slider dragged, a choice, a switch
    s.Fold(150.f, aimHead);
    s.Click(StripX(0.75f), 192.f);
    s.Down(StripX(0.5f), 248.f).Move(StripX(0.25f), 248.f)("up");
    s.Click(383.f, 395.5f);
    s.Click(393.f, 432.f);
    s.Fold(150.f, aimHead);

    // Reach's card: both ends of a range, a value typed
    s.Fold(150.f, reachHead);
    s.Click(StripX(0.25f), 268.f);
    s.Click(StripX(0.9f), 268.f);
    s.Click(391.f, 299.f)("type 42");
    s.Key(VK_RETURN);
    s.Fold(150.f, reachHead);

    // keys: a letter, a side button, the right button, a left click cancels, Escape clears
    s.Click(BindX(kRight), RowMid(0)).Key('K');
    s.Click(BindX(kRight), RowMid(1)).Click(640.f, 540.f, 3);
    s.Click(BindX(kLeft), RowMid(3)).Click(640.f, 540.f, 1);
    s.Click(BindX(kLeft), RowMid(2)).Click(640.f, 540.f);
    s.Click(BindX(kRight), RowMid(2)).Key('V');
    s.Click(BindX(kRight, 34.f), RowMid(2)).Key(VK_ESCAPE);

    // the wheel moves the cards, and what moved still answers where it is
    s.Fold(150.f, aimHead);
    s("wheel -3")("wait 450");
    s.Click(393.f, 597.f - 300.f + 32.f);
    // the scrollbar takes them back
    s.Down(851.f, 300.f).Move(851.f, 62.f)("up");
    s.Click(SwitchX(kLeft), RowMid(0));
    s.Fold(150.f, aimHead);

    // a list: two names in, the first one out
    s.Fold(150.f, RowMid(4));
    s.Click(200.f, 510.f)("type Steve");
    s.Key(VK_RETURN);
    s.Click(200.f, 510.f)("type Alex");
    s.Key(VK_RETURN);
    s.Click(390.f, 547.f);

    // a long choice steps
    s.Fold(600.f, RowMid(3));
    s.Click(812.5f, 399.5f);

    // the other tabs
    s.Tab(314.f);
    s.Click(SwitchX(kLeft), RowMid(1));
    s.Click(SwitchX(kRight), RowMid(3));
    s.Click(406.f, CardY(2) + 25.f);   // the far corner of a switch is still the switch

    s.Tab(394.f);
    s.Click(SwitchX(kLeft), RowMid(0));
    s.Click(BindX(kLeft), RowMid(0)).Key('Q');

    s.Tab(464.f);
    s.Click(SwitchX(kRight), RowMid(1));

    s.Tab(542.f);
    s.Click(327.f, 139.f);
    s.Click(393.f, 236.f);
    return s.text;
}

// A second session for the accent picker, which moves the cards under it.
std::string PickerScript() {
    Session s;
    s.Tab(542.f);
    s.Click(399.f, 139.f)("wait 100");
    s.Click(134.f, 219.f);
    return s.text;
}

std::map<std::string, ModuleState> Run(const std::string& exe, const std::string& script, float scale, unsigned* accent) {
    const std::string dir = exe.substr(0, exe.find_last_of("\\/") + 1);
    const std::string scriptPath = dir + "ui_script.txt", dumpPath = dir + "ui_state.txt";
    std::ofstream(scriptPath, std::ios::binary) << script;
    DeleteFileA(dumpPath.c_str());

    char scaleText[16];
    std::snprintf(scaleText, sizeof scaleText, "%.2f", scale);
    std::string cmd = "\"" + exe + "\" --shot --shotscale=" + scaleText + " --script=\"" + scriptPath + "\" --dump=\"" + dumpPath + "\"";
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
    if (WaitForSingleObject(pi.hProcess, 90000) != WAIT_OBJECT_0) {
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
    return s.mods;
}

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

}  // namespace

int main(int argc, char** argv) {
    char full[MAX_PATH] = {};
    GetFullPathNameA(argc > 1 ? argv[1] : "build/CitronLite.exe", MAX_PATH, full, nullptr);
    const std::string exe = full;
    for (float scale : {1.f, 0.75f, 1.25f, 1.5f, 2.f}) {
        g_scale = scale;
        unsigned accent = 0;
        auto m = Run(exe, Script(), scale, &accent);

        Check(m["triggerbot"].on, "the switch in the right column");
        Check(m["hitbox"].on, "the first click after moving the window");

        auto& aim = m["aimassist"];
        Check(Near(aim.sliders["strength"], 78.f, 1.f), "a click on a slider: strength " + std::to_string(aim.sliders["strength"]));
        Check(Near(aim.sliders["smoothing"], 25.f, 1.f), "a dragged slider: smoothing " + std::to_string(aim.sliders["smoothing"]));
        Check(aim.choices["aimpart"] == 2, "a segmented choice");
        Check(!aim.checks["onlyLmb"], "a switch in a card");
        Check(!aim.on, "the wheel and the scrollbar: back at the top, Aim Assist's switch answers");

        auto& reach = m["reach"];
        Check(Near(reach.sliders["distance_min"], 3.75f, 0.02f), "a range's low end: " + std::to_string(reach.sliders["distance_min"]));
        Check(Near(reach.sliders["distance_max"], 5.7f, 0.02f), "a range's high end: " + std::to_string(reach.sliders["distance_max"]));
        Check(reach.sliders["chance"] == 42.f, "a typed value: " + std::to_string(reach.sliders["chance"]));
        Check(reach.on, "a switch moved by the wheel");

        Check(m["triggerbot"].key == "K", "a letter for a key: '" + m["triggerbot"].key + "'");
        Check(m["hitbox"].key == "Mouse4", "a side button for a key: '" + m["hitbox"].key + "'");
        Check(m["rightclicker"].key == "RMB", "the right button for a key: '" + m["rightclicker"].key + "'");
        Check(m["ghosthand"].key.empty(), "a left click cancels: '" + m["ghosthand"].key + "'");
        Check(m["clicker"].key.empty(), "Escape clears: '" + m["clicker"].key + "'");

        Check(m["friends"].names == std::vector<std::string>({"Alex"}), "the friends list");
        Check(m["teams"].choices["color"] == 1, "a stepped choice");

        Check(m["stap"].on && m["fastfall"].on, "the switches on Movement");
        Check(m["autosprint"].on, "the far corner of a switch");
        Check(m["keybindpearl"].on && m["keybindpearl"].key == "Q", "Items");
        Check(m["tpnametags"].on, "Visuals");
        Check(accent == 0x38E1FF, "a swatch");
        Check(m["streamhide"].on, "a switch in Settings");

        unsigned picked = 0;
        Run(exe, PickerScript(), scale, &picked);
        Check(picked != 0 && picked != 0xC8F135, "the colour picker");
        std::printf("  scale %.2f done\n", scale);
    }
    if (g_failed) {
        std::printf("%d checks failed\n", g_failed);
        return 1;
    }
    std::printf("window session tests passed\n");
    return 0;
}
