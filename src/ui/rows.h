// What a module's row says, in the panel and the Click GUI alike. Auto Clicker is two modules behind
// one row: the left side and the hidden right side, picked in its settings. The row is lit while either
// side is on, and its switch and key work the side that is picked.
#pragma once
#include "citron/model.h"
#include <string>

namespace citron::ui {

inline int g_clickerSide = 0;   // 0 left, 1 right

inline bool IsClicker(const ModuleDef& d) { return d.key == "clicker"; }

inline const ModuleDef& SideDef(const AppState& s, const ModuleDef& d) {
    if (IsClicker(d) && g_clickerSide == 1)
        if (const ModuleDef* right = s.Def("rightclicker")) return *right;
    return d;
}

inline bool StateOn(const AppState& s, const std::string& key) {
    const ModuleState* st = s.St(key);
    return st && st->on;
}

inline bool RowOn(const AppState& s, const ModuleDef& d) { return StateOn(s, d.key) || (IsClicker(d) && StateOn(s, "rightclicker")); }

// The key a row shows: the picked side's, else the other side's.
inline std::string RowKey(const AppState& s, const ModuleDef& d) {
    const ModuleState* st = s.St(SideDef(s, d).key);
    if (st && !st->key.empty()) return st->key;
    if (IsClicker(d))
        for (const char* key : {"clicker", "rightclicker"})
            if (const ModuleState* side = s.St(key); side && !side->key.empty()) return side->key;
    return {};
}

// A click on a row's light: lit, Auto Clicker's goes out for both sides.
inline void ToggleRow(AppState& s, const ModuleDef& d) {
    if (IsClicker(d) && RowOn(s, d)) {
        s.St("clicker")->on = s.St("rightclicker")->on = false;
    } else if (ModuleState* st = s.St(SideDef(s, d).key)) {
        st->on = !st->on;
    }
}

}  // namespace citron::ui
