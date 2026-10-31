// What is drawn over the game.
#pragma once
#include "citron/model.h"
#include <string>
#include <vector>

namespace citron {

namespace ui { struct R; }

constexpr float kToastSeconds = 2.2f;

struct Toast {
    std::string title, state;
    bool lit = false;
    unsigned long long atMs = 0;
};

// `t` is how far it has come in, 0 to 1.
void DrawClickGui(AppState& s, const ui::R& view, float t);
void DrawToasts(const std::vector<Toast>& toasts, const ui::R& view, unsigned long long nowMs);

}  // namespace citron
