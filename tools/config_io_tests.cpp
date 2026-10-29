// Configs: what is written comes back the same, old 1.x files keep their numbers, and what the
// catalog no longer has is dropped.
#include "citron/config_io.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool Same(const citron::ModuleState& a, const citron::ModuleState& b) {
    return a.on == b.on && a.key == b.key && a.sliders == b.sliders && a.checks == b.checks &&
           a.choices == b.choices && a.names == b.names;
}

// A file read the way the app reads one: over the defaults, then kept to the catalog.
std::map<std::string, citron::ModuleState> Load(const std::string& text, unsigned* accent = nullptr) {
    citron::AppState s;
    citron::BuildModel(s);
    Require(citron::cfgio::Deserialize(text.c_str(), s.mods, accent), "the parser rejected a file");
    citron::cfgio::KeepKnown(s.mods, s.defs);
    return s.mods;
}

}  // namespace

int main() {
    citron::AppState defaults;
    citron::BuildModel(defaults);

    // the defaults come back as they went out
    {
        unsigned accent = 0;
        const auto back = Load(citron::cfgio::Serialize(defaults.mods, 0x38E1FF), &accent);
        Require(accent == 0x38E1FF, "the accent changed");
        Require(back.size() == defaults.mods.size(), "a module was lost");
        for (const auto& [id, st] : defaults.mods) Require(back.count(id) && Same(back.at(id), st), "a module changed");
    }

    // so does an edited one
    {
        auto mods = defaults.mods;
        auto& aim = mods.at("aimassist");
        aim.on = true;
        aim.key = "Mouse5";
        aim.sliders["strength"] = 72.f;
        aim.sliders["distance"] = 3.85f;
        aim.checks["onlyLmb"] = false;
        aim.choices["aimpart"] = 2;
        auto& friends = mods.at("friends");
        friends.on = true;
        friends.names = {"Steve", "x_Alex", "Name With Spaces"};
        mods.at("taskbarhide").key.clear();   // its default is Insert
        mods.at("velocity").sliders["horizontal_min"] = 12.f;
        const auto back = Load(citron::cfgio::Serialize(mods, 0));
        for (const auto& [id, st] : mods) Require(Same(back.at(id), st), "an edited module changed");
        Require(back.at("taskbarhide").key.empty(), "a cleared bind came back as the default");
    }

    // Windows line endings, and a list in the file replaces the one in the state
    {
        const auto back = Load("citron-config 2\r\n[friends]\r\non 1\r\nn One\r\nn Two\r\n");
        Require(back.at("friends").on, "on was lost");
        Require(back.at("friends").names == std::vector<std::string>({"One", "Two"}), "the names were misread");
    }

    // a 1.x file: one clicker card held both sides, the right side's numbers move to Right Clicker
    {
        const auto back = Load(
            "citron-config 1\n"
            "[clicker]\non 1\nkey F\ns l_mincps 9\ns l_maxcps 13.5\ns l_jitter 2\ns r_mincps 6\ns r_maxcps 10\n"
            "s r_jitter 1\nc breakBlock 0\n"
            "[rightclicker]\non 1\nkey G\n"
            "[aimassist]\non 1\ns vertical 20\ns strength 60\nc sticky 1\nc onlyLmb 0\n"
            "[taskbarhide]\nc hideWindow 1\n");
        const auto& left = back.at("clicker");
        const auto& right = back.at("rightclicker");
        Require(left.on && left.key == "F", "the clicker's toggle or bind");
        Require(left.sliders.at("cps_min") == 9.f && left.sliders.at("cps_max") == 13.5f, "the left CPS did not move over");
        Require(left.sliders.at("jitter") == 2.f && !left.checks.at("breakBlock"), "the left jitter or a check");
        Require(right.on && right.key == "G", "Right Clicker's toggle or bind");
        Require(right.sliders.at("cps_min") == 6.f && right.sliders.at("cps_max") == 10.f && right.sliders.at("jitter") == 1.f,
                "the right side's numbers did not move to Right Clicker");
        Require(!left.sliders.count("l_mincps") && !left.sliders.count("r_maxcps"), "old names stayed behind");
        const auto& aim = back.at("aimassist");
        Require(aim.on && aim.sliders.at("strength") == 60.f && !aim.checks.at("onlyLmb"), "Aim Assist's numbers");
        Require(!aim.sliders.count("vertical") && !aim.checks.count("sticky"), "retired Aim Assist settings stayed");
        Require(back.at("taskbarhide").key == "Insert", "a file that names no bind keeps the default");
        Require(back.at("taskbarhide").checks.at("hideWindow"), "Taskbar Hide's check");
    }

    // what the catalog does not have goes, and values come back inside their ends
    {
        const auto back = Load(
            "[esp]\non 1\n[airjump]\non 1\n"
            "[reach]\non 1\ns distance_min 9\ns distance_max 2\n"
            "[hitbox]\ns width nan\n"
            "[aimassist]\nh aimpart 7\ns strength -5\n"
            "[selfdestruct]\non 1\n"
            "[zoom]\nkey none\n");
        Require(!back.count("esp") && !back.count("airjump"), "a retired module stayed");
        Require(back.size() == defaults.mods.size(), "the module set changed");
        const auto& reach = back.at("reach");
        Require(reach.sliders.at("distance_min") == 3.f && reach.sliders.at("distance_max") == 6.f,
                "a range outside its ends, the wrong way round");
        Require(back.at("hitbox").sliders.at("width") == defaults.mods.at("hitbox").sliders.at("width"),
                "a number that is not one");
        Require(back.at("aimassist").choices.at("aimpart") == 0, "a choice past the last option");
        Require(back.at("aimassist").sliders.at("strength") == 10.f, "a slider below its low end");
        Require(!back.at("selfdestruct").on, "a module without a toggle came back on");
        Require(back.at("zoom").key.empty(), "key none is no bind");
    }

    // the old empty bind and a missing header
    {
        const auto back = Load("[zoom]\non 1\nkey NONE\n");
        Require(back.at("zoom").on && back.at("zoom").key.empty(), "1.x wrote no bind as NONE");
    }

    std::cout << "config tests passed\n";
    return 0;
}
