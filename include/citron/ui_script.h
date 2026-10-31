// A scripted session for the UI tests. Each step reaches the window as a real window message, at
// design coordinates, so one script tries every window scale the same way. One step a line:
//
//   move X Y          click X Y [BUTTON]      down X Y [BUTTON]     up [BUTTON]
//   wheel NOTCHES     key VK                  type TEXT
//   wait MS           shot PATH
//
// BUTTON is 0 left (the default), 1 right, 2 middle, 3 and 4 the side buttons.
#pragma once
#include "citron/platform.h"
#include <string>
#include <vector>

namespace citron {

class UiScript {
public:
    bool Load(const std::string& path);
    // Once a frame, before it is drawn. False when the script has run out; a shot step names its
    // file in `capture`.
    bool Step(platform::Window& w, std::string& capture);

private:
    struct Line {
        std::string op, text;
        float a = 0.f, b = 0.f;
        int button = 0;
    };
    std::vector<Line> lines;
    size_t next = 0;
    int settle = 0;
    unsigned long long until = 0;
    bool pointing = false;
    LPARAM pointer = 0;
};

}  // namespace citron
