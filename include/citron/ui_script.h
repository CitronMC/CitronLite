// A scripted session for the UI tests. Each step reaches the window as a real window message, at
// design coordinates, so one script tries every window scale the same way. One step a line:
//
//   move X Y          click X Y [BUTTON]      down X Y [BUTTON]     up [BUTTON]
//   wheel NOTCHES     key VK                  type TEXT
//   wait MS           shot PATH               rects PATH
//
// BUTTON is 0 left (the default), 1 right, 2 middle, 3 and 4 the side buttons. In move, click and down
// the place can be @ID [FX FY]: the control of that id where it is drawn right now, at that fraction of
// its width and height (the middle by default). rects writes where every control is, a line each.
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
    // The ids a step named that were not on screen when it ran.
    const std::vector<std::string>& Missing() const { return missing; }

private:
    struct Line {
        std::string op, text, target;
        float a = 0.f, b = 0.f;
        int button = 0;
    };
    std::vector<Line> lines;
    std::vector<std::string> missing;
    size_t next = 0;
    int settle = 0;
    unsigned long long until = 0;
    bool pointing = false, lost = false;
    LPARAM pointer = 0;
};

}  // namespace citron
