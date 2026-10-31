#include "citron/app.h"
#include "citron/config_io.h"
#include "citron/telemetry.h"
#include "citron/updater.h"
#include "citron/version.h"
#include "draw.h"
#include "imgui_internal.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace citron {

using namespace ui;

namespace {

constexpr float kHeaderH = 60.f, kCardHeadH = 64.f, kRowGap = 14.f;
constexpr float kCfgRowH = 48.f, kCfgRowGap = 4.f;
R g_cfgList;   // where the config list was drawn, for the wheel

void Open(const wchar_t* url) { ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); }

std::string ImGuiKeyLabel(ImGuiKey k) {
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return std::string(1, (char)('A' + (k - ImGuiKey_A)));
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return std::string(1, (char)('0' + (k - ImGuiKey_0)));
    if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return "Num" + std::to_string(k - ImGuiKey_Keypad0);
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F24) return "F" + std::to_string(1 + (k - ImGuiKey_F1));
    switch (k) {
    case ImGuiKey_Tab: return "Tab";
    case ImGuiKey_LeftArrow: return "Left";
    case ImGuiKey_RightArrow: return "Right";
    case ImGuiKey_UpArrow: return "Up";
    case ImGuiKey_DownArrow: return "Down";
    case ImGuiKey_PageUp: return "PageUp";
    case ImGuiKey_PageDown: return "PageDown";
    case ImGuiKey_Home: return "Home";
    case ImGuiKey_End: return "End";
    case ImGuiKey_Insert: return "Insert";
    case ImGuiKey_Delete: return "Del";
    case ImGuiKey_Backspace: return "Back";
    case ImGuiKey_Space: return "Space";
    case ImGuiKey_Enter: return "Enter";
    case ImGuiKey_LeftCtrl: return "Ctrl";
    case ImGuiKey_RightCtrl: return "RCtrl";
    case ImGuiKey_LeftShift: return "Shift";
    case ImGuiKey_RightShift: return "RShift";
    case ImGuiKey_LeftAlt: case ImGuiKey_RightAlt: return "Alt";
    case ImGuiKey_Menu: return "Menu";
    case ImGuiKey_Apostrophe: return "Apostrophe";
    case ImGuiKey_Comma: return "Comma";
    case ImGuiKey_Minus: return "Minus";
    case ImGuiKey_Period: return "Period";
    case ImGuiKey_Slash: return "Slash";
    case ImGuiKey_Semicolon: return "Semicolon";
    case ImGuiKey_Equal: return "Equal";
    case ImGuiKey_LeftBracket: return "LeftBracket";
    case ImGuiKey_Backslash: return "Backslash";
    case ImGuiKey_RightBracket: return "RightBracket";
    case ImGuiKey_GraveAccent: return "GraveAccent";
    case ImGuiKey_CapsLock: return "CapsLock";
    case ImGuiKey_ScrollLock: return "ScrollLock";
    case ImGuiKey_NumLock: return "NumLock";
    case ImGuiKey_PrintScreen: return "PrintScreen";
    case ImGuiKey_Pause: return "Pause";
    case ImGuiKey_KeypadDecimal: return "NumDecimal";
    case ImGuiKey_KeypadDivide: return "NumDivide";
    case ImGuiKey_KeypadMultiply: return "NumMultiply";
    case ImGuiKey_KeypadSubtract: return "NumSubtract";
    case ImGuiKey_KeypadAdd: return "NumAdd";
    default: return "";
    }
}

float ChoiceHeight() { return 27.f; }

bool UseStepper(const ChoiceDef& c, float width) {
    return c.options.size() > 4 || SegmentedWidth(c.options) > width * 0.62f;
}

}  // namespace

void App::SyncPicker() {
    const unsigned a = s.accent ? s.accent : kDefaultAccent;
    float h, sa, v;
    ImGui::ColorConvertRGBtoHSV(((a >> 16) & 255) / 255.f, ((a >> 8) & 255) / 255.f, (a & 255) / 255.f, h, sa, v);
    if (sa > 0.f && v > 0.f) pickerH = h;
    pickerS = sa;
    pickerV = v;
}

bool App::IsDragHandle(float x, float y) const {
    if (x < 0.f || x >= kPanelW || y < 0.f || y >= kHeaderH) return false;
    if (x < 170.f) return true;
    return x > tabsEnd + 4.f && x < controlsX - 8.f;
}

// A module card.
float App::CardBodyHeight(const ModuleDef& d, const ModuleState& st) const {
    float h = 0.f;
    int rows = 0;
    auto add = [&](float rowH) {
        h += rowH;
        ++rows;
    };
    for (const auto& sl : d.sliders) add(42.f), (void)sl;
    for (const auto& c : d.choices) add(ChoiceHeight()), (void)c;
    for (const auto& c : d.checks) add(18.f), (void)c;
    if (d.names) add(32.f + (st.names.empty() ? 0.f : 6.f + (float)st.names.size() * 34.f - 4.f));
    if (!rows) return 0.f;
    return 1.f + 14.f + h + kRowGap * (float)(rows - 1) + 16.f;
}

void App::DrawCardBody(ImDrawList* dl, const ModuleDef& d, ModuleState& st, float x, float y, float w) {
    char id[128];
    float sy = y;
    for (const auto& sl : d.sliders) {
        std::snprintf(id, sizeof id, "m:%s:%s", d.key.c_str(), sl.key.c_str());
        if (sl.range) {
            float& lo = st.sliders[sl.key + "_min"];
            float& hi = st.sliders[sl.key + "_max"];
            RangeUI(dl, id, R{x, sy, w, 42.f}, sl.label.c_str(), lo, hi, sl.min, sl.max, sl.step, sl.unit.c_str());
        } else {
            SliderUI(dl, id, R{x, sy, w, 42.f}, sl.label.c_str(), st.sliders[sl.key], sl.min, sl.max, sl.step, sl.unit.c_str());
        }
        sy += 42.f + kRowGap;
    }
    for (const auto& c : d.choices) {
        std::snprintf(id, sizeof id, "m:%s:%s", d.key.c_str(), c.key.c_str());
        TextAt(dl, F(400, 13.f), x, sy + (27.f - 18.f) * 0.5f + LM(13.f).asc, Hex(kLabel), c.label.c_str());
        int& sel = st.choices[c.key];
        if (UseStepper(c, w)) StepperUI(dl, id, x + w, sy, c.options, sel);
        else SegmentedUI(dl, id, x + w, sy, c.options, sel);
        sy += ChoiceHeight() + kRowGap;
    }
    for (const auto& c : d.checks) {
        std::snprintf(id, sizeof id, "m:%s:%s", d.key.c_str(), c.key.c_str());
        bool on = st.checks.count(c.key) ? st.checks[c.key] : c.def;
        SwitchRow(dl, id, R{x, sy, w, 18.f}, c.label.c_str(), on);
        st.checks[c.key] = on;
        sy += 18.f + kRowGap;
    }
    if (d.names) DrawFriends(dl, st, x, sy, w);
}

// The friends list: a field to add a name, then one row per name with its remove button.
float App::DrawFriends(ImDrawList* dl, ModuleState& st, float x, float y, float w) {
    std::string typed;
    if (TextField(dl, "friends:add", R{x, y, w, 32.f}, "", "Add a name, or middle click a player in game", typed)) {
        const size_t a = typed.find_first_not_of(' '), b = typed.find_last_not_of(' ');
        const std::string name = a == std::string::npos ? std::string() : typed.substr(a, b - a + 1);
        if (!name.empty() && std::find(st.names.begin(), st.names.end(), name) == st.names.end()) st.names.push_back(name);
    }
    float ry = y + 32.f + 6.f;
    int remove = -1;
    for (size_t i = 0; i < st.names.size(); ++i) {
        const R row{x, ry, w, 30.f};
        char id[64];
        std::snprintf(id, sizeof id, "friends:%d:x", (int)i);
        const R xb{row.x1() - 30.f, row.y + 3.f, 24.f, 24.f};
        const Hit hx = Item(id, xb);
        Pointer(hx);
        if (hx.clicked) remove = (int)i;
        Fill(dl, row, White(hx.hovered ? 0.06f : 0.035f), 6.f);
        TextAt(dl, F(400, 13.f), row.x + 10.f, row.y + 6.f + LM(13.f).asc, Hex(kText),
               Ellipsize(F(400, 13.f), st.names[i], w - 50.f).c_str());
        DrawCross(dl, xb.x + 12.f, xb.y + 12.f, 4.f, Hex(hx.hovered ? kDanger : kMuted), 1.5f);
        ry += 34.f;
    }
    if (remove >= 0) st.names.erase(st.names.begin() + remove);
    return ry - y;
}

float App::DrawCard(ImDrawList* dl, const ModuleDef& d, ModuleState& st, float x, float y, float w) {
    char id[96];
    const float bodyH = CardBodyHeight(d, st);
    const bool open = bodyH > 0.f && s.expanded == d.key;
    std::snprintf(id, sizeof id, "m:%s:h", d.key.c_str());
    const float h = Animate(ImHashStr(id), open ? kCardHeadH + bodyH : kCardHeadH, 0.2f);
    const R card{x, y, w, h};
    Fill(dl, card, Hex(kCardBg), 10.f);

    float right = card.x1() - 16.f;
    if (d.hasToggle) {
        const R sw{right - kSwitchW, card.y + 23.f, kSwitchW, kSwitchH};
        std::snprintf(id, sizeof id, "m:%s:sw", d.key.c_str());
        const Hit hs = Item(id, sw);
        Pointer(hs);
        if (hs.clicked) st.on = !st.on;
        DrawSwitch(dl, sw, st.on, id);
        right = sw.x - 12.f;
    }
    if (d.bind != BindKind::None) {
        const std::string label = st.key.empty() ? "NONE" : st.key;
        const float bw = BindButtonWidth(label);
        const R bb{right - bw, card.y + 20.f, bw, 24.f};
        std::snprintf(id, sizeof id, "m:%s:bind", d.key.c_str());
        const bool capturing = s.listening == d.key;
        if (BindButton(dl, id, bb, label, capturing).clicked && s.listening.empty()) s.listening = d.key;
        right = bb.x - 12.f;
    }
    std::snprintf(id, sizeof id, "m:%s:hdr", d.key.c_str());
    const Hit hh = Item(id, R{card.x, card.y, card.w, kCardHeadH});
    if (bodyH > 0.f) {
        Pointer(hh);
        if (hh.clicked) s.expanded = open ? std::string() : d.key;
    }
    TextAt(dl, F(600, 14.f), card.x + 16.f, card.y + 14.f + LM(14.f).asc, Hex(kText), d.name.c_str());
    TextAt(dl, F(400, 11.5f), card.x + 16.f, card.y + 35.f + LM(11.5f).asc, Hex(kMuted),
           Ellipsize(F(400, 11.5f), d.desc, right - (card.x + 16.f)).c_str());

    if (h > kCardHeadH + 0.5f) {
        const float progress = bodyH > 0.f ? std::clamp((h - kCardHeadH) / bodyH, 0.f, 1.f) : 1.f;
        const float outer = g_alpha;
        g_alpha = outer * progress;
        PushClip(card);
        Fill(dl, R{card.x, card.y + kCardHeadH, card.w, 1.f}, White(0.05f));
        DrawCardBody(dl, d, st, card.x + 16.f, card.y + kCardHeadH + 15.f, card.w - 32.f);
        PopClip();
        g_alpha = outer;
    }
    return h;
}

// The header.
void App::DrawHeader(ImDrawList* dl, FrameOutput& out, ImTextureID mark) {
    const R logo{18.f, 19.f, 22.f, 22.f};
    if (mark) dl->AddImage(mark, Min(logo), Max(logo), ImVec2(0, 0), ImVec2(1, 1), Accent());
    const float baseline = (kHeaderH - 21.f) * 0.5f + LM(15.f).asc;
    TextAt(dl, F(700, 15.f), 48.f, baseline, Hex(kText), "Citron");
    TextAt(dl, F(700, 15.f), 48.f + TextW(F(700, 15.f), "Citron ", 0.f), baseline, Hex(kMuted), "Lite");

    float tx = 188.f;
    for (const CatInfo& c : kCats) {
        ImFont* f = F(600, 13.f);
        const R b{tx, 13.f, 28.f + TextW(f, c.label), 34.f};
        char id[32];
        std::snprintf(id, sizeof id, "tab:%s", c.key);
        const Hit h = Item(id, b);
        Pointer(h);
        if (h.clicked && s.category != c.cat) {
            s.category = c.cat;
            scrollY = scrollTarget = 0.f;
            tabSwitchAt = g_now;
        }
        const bool active = s.category == c.cat;
        const float bg = Animate(ImHashStr(id, 0, 0x1000), active ? 0.06f : 0.f, 0.15f);
        const float k = Animate(ImHashStr(id, 0, 0x2000), active || h.hovered ? 1.f : 0.f, 0.15f);
        Fill(dl, b, White(bg), 7.f);
        TextAt(dl, f, b.x + 14.f, b.y + 8.f + LM(13.f).asc, Mix(Hex(kMuted), Hex(kText), k), c.label);
        tx += b.w + 4.f;
    }
    tabsEnd = tx - 4.f;

    // window controls
    const R close{kPanelW - 14.f - 32.f, 14.f, 32.f, 32.f}, mini{close.x - 4.f - 32.f, 14.f, 32.f, 32.f};
    controlsX = mini.x;
    {
        const Hit h = Item("close", close);
        Pointer(h);
        if (h.clicked) out.wantClose = true;
        if (h.hovered) Fill(dl, close, White(0.06f), 8.f);
        DrawCross(dl, close.x + 16.f, close.y + 16.f, 5.f, Hex(h.hovered ? kText : kMuted), 1.6f);
    }
    {
        const Hit h = Item("minimize", mini);
        Pointer(h);
        if (h.clicked) out.wantMinimize = true;
        if (h.hovered) Fill(dl, mini, White(0.06f), 8.f);
        Line(dl, mini.x + 11.f, mini.y + 16.5f, mini.x + 21.f, mini.y + 16.5f, Hex(h.hovered ? kText : kMuted), 1.6f);
    }

    // the game, once it has been found
    const BackendStatus st = backend.Status();
    if (st.kind != BackendStatus::Kind::Searching) {
        std::string text;
        unsigned dot = g_accent;
        if (st.kind == BackendStatus::Kind::Unsupported) {
            text = "Unsupported version";
            dot = kDanger;
        } else {
            const size_t cut = st.version.rfind('.');
            text = "Minecraft " + (cut != std::string::npos ? st.version.substr(0, cut) : st.version);
            if (st.kind == BackendStatus::Kind::Partial) dot = kWarn;
        }
        ImFont* f = F(600, 11.5f);
        const float w = 10.f + 6.f + 7.f + TextW(f, text.c_str()) + 11.f;
        const R pill{controlsX - 10.f - w, 20.f, w, 20.f};
        if (pill.x > tabsEnd + 8.f) {
            Fill(dl, pill, White(0.05f), 10.f);
            Circle(dl, pill.x + 13.f, pill.y + 10.f, 3.f, Hex(dot));
            TextAt(dl, f, pill.x + 23.f, pill.y + 3.f + LM(11.5f).asc, Hex(kLabel), text.c_str());
            const Hit h = Item("status", pill);
            if (h.hovered && (!st.detail.empty() || st.kind == BackendStatus::Kind::Unsupported)) {
                const std::string tip = st.detail.empty() ? "Citron Lite runs on 1.26.52 and 1.26.60" : st.detail;
                const float tw = TextW(F(400, 12.f), tip.c_str()) + 20.f;
                const R box{std::max(8.f, pill.x1() - tw), pill.y1() + 6.f, tw, 26.f};
                ShadowRect(dl, box, 7.f, 4.f, 12.f, 0.4f);
                Fill(dl, box, Hex(0x1f232c), 7.f);
                TextAt(dl, F(400, 12.f), box.x + 10.f, box.y + 5.f + LM(12.f).asc, Hex(kText), tip.c_str());
            }
        }
    }
}

// Settings.
float App::DrawThemeCard(ImDrawList* dl, float x0, float y, float w) {
    const float boxH = 110.f;
    const float h = 16.f + 19.f + 16.f + 18.f + (pickerOpen ? 16.f + boxH : 0.f) + 16.f;
    Fill(dl, R{x0, y, w, h}, Hex(kCardBg), 10.f);
    const float x = x0 + 16.f, iw = w - 32.f;
    float cy = y + 16.f;
    TextAt(dl, F(600, 14.f), x, cy + LM(14.f).asc, Hex(kText), "Theme");
    cy += 19.f + 16.f;
    TextAt(dl, F(400, 13.f), x, cy + LM(13.f).asc, Hex(kLabel), "Accent");
    const unsigned accent = s.accent ? s.accent : kDefaultAccent;
    const float size = 18.f, rad = 9.f, gap = 6.f;
    const float xs = x + iw - (7.f * size + 6.f * gap), dotY = cy + 9.f;
    bool preset = false;
    for (unsigned c : kSwatches) preset |= c == accent;
    for (int i = 0; i < 6; ++i) {
        const float cx = xs + rad + (float)i * (size + gap);
        char id[32];
        std::snprintf(id, sizeof id, "th:c%d", i);
        const Hit hc = Item(id, R{cx - rad, dotY - rad, size, size});
        Pointer(hc);
        if (hc.clicked) {
            s.accent = kSwatches[i] == kDefaultAccent ? 0u : kSwatches[i];
            SyncPicker();
            pickerOpen = false;
        }
        if (accent == kSwatches[i]) SelectionRing(dl, cx, dotY, rad, kCardBg);
        Circle(dl, cx, dotY, rad, Hex(kSwatches[i]));
    }
    {
        const float cx = xs + rad + 6.f * (size + gap);
        const Hit hc = Item("th:custom", R{cx - rad, dotY - rad, size, size});
        Pointer(hc);
        if (hc.clicked) pickerOpen = !pickerOpen;
        if (!preset) {
            SelectionRing(dl, cx, dotY, rad, kCardBg);
            Circle(dl, cx, dotY, rad, Hex(accent));
        } else {
            DrawHueWheel(dl, cx, dotY, rad);
        }
    }
    cy += 18.f;
    if (pickerOpen) {
        cy += 16.f;
        const R sv{x, cy, 200.f, boxH}, hue{x + 210.f, cy, 14.f, boxH};
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const Hit hs = Item("th:sv", sv);
        Pointer(hs);
        if (hs.active) {
            pickerS = std::clamp((mouse.x / g_s - sv.x) / sv.w, 0.f, 1.f);
            pickerV = 1.f - std::clamp((mouse.y / g_s - sv.y) / sv.h, 0.f, 1.f);
            s.accent = HsvToRgb(pickerH, pickerS, pickerV);
        }
        const Hit hh = Item("th:hue", hue);
        Pointer(hh);
        if (hh.active) {
            pickerH = std::min(std::clamp((mouse.y / g_s - hue.y) / hue.h, 0.f, 1.f), 0.9999f);
            s.accent = HsvToRgb(pickerH, pickerS, pickerV);
        }
        const ImU32 hueCol = Hex(HsvToRgb(pickerH, 1.f, 1.f));
        dl->AddRectFilledMultiColor(Min(sv), Max(sv), White(1.f), hueCol, hueCol, White(1.f));
        dl->AddRectFilledMultiColor(Min(sv), Max(sv), Black(0.f), Black(0.f), Black(1.f), Black(1.f));
        for (int i = 0; i < 6; ++i) {
            const R seg{hue.x, hue.y + hue.h * (float)i / 6.f, hue.w, hue.h / 6.f};
            const ImU32 top = Hex(HsvToRgb((float)i / 6.f, 1.f, 1.f)), bottom = Hex(HsvToRgb(i == 5 ? 0.f : (float)(i + 1) / 6.f, 1.f, 1.f));
            dl->AddRectFilledMultiColor(Min(seg), Max(seg), top, top, bottom, bottom);
        }
        const ImVec2 mk(Dv(sv.x + pickerS * sv.w), Dv(sv.y + (1.f - pickerV) * sv.h));
        dl->AddCircle(mk, Dv(6.5f), Black(0.5f), 0, Dv(1.f));
        dl->AddCircle(mk, Dv(5.5f), White(1.f), 0, Dv(1.5f));
        const float hy = hue.y + pickerH * hue.h;
        Fill(dl, R{hue.x - 2.f, hy - 2.f, hue.w + 4.f, 4.f}, White(1.f), 1.f);
        char hex[16];
        std::snprintf(hex, sizeof hex, "#%06X", accent & 0xFFFFFFu);
        TextAt(dl, F(600, 11.f), x + 240.f, cy + LM(11.f).asc, Hex(kMuted), "HEX", 0.66f);
        TextAt(dl, F(600, 14.f), x + 240.f, cy + 19.f + LM(14.f).asc, Hex(kText), hex);
    }
    return h;
}

float App::DrawClientCard(ImDrawList* dl, float x0, float y, float w) {
    ModuleState* stream = s.St("streamhide");
    ModuleState* task = s.St("taskbarhide");
    ModuleState* sd = s.St("selfdestruct");
    const float h = 16.f + 19.f + 16.f + (18.f + kRowGap) + (24.f + kRowGap) + (18.f + 20.f) + (24.f + kRowGap) +
                    3.f * (18.f + kRowGap) - kRowGap + 16.f;
    Fill(dl, R{x0, y, w, h}, Hex(kCardBg), 10.f);
    const float x = x0 + 16.f, iw = w - 32.f;
    float cy = y + 16.f;
    TextAt(dl, F(600, 14.f), x, cy + LM(14.f).asc, Hex(kText), "Window");
    cy += 19.f + 16.f;
    if (stream) SwitchRow(dl, "set:stream", R{x, cy, iw, 18.f}, "Stream Hide", stream->on);
    cy += 18.f + kRowGap;
    auto keyRow = [&](const char* id, const char* label, ModuleState* st, const char* module, bool toggle) {
        TextAt(dl, F(400, 13.f), x, cy + 3.f + LM(13.f).asc, Hex(kLabel), label);
        float right = x + iw;
        if (toggle && st) {
            char sid[48];
            std::snprintf(sid, sizeof sid, "%s:sw", id);
            const R sw{right - kSwitchW, cy + 3.f, kSwitchW, kSwitchH};
            const Hit hs = Item(sid, sw);
            Pointer(hs);
            if (hs.clicked) st->on = !st->on;
            DrawSwitch(dl, sw, st->on, sid);
            right = sw.x - 12.f;
        }
        if (st) {
            const std::string lbl = st->key.empty() ? "NONE" : st->key;
            const float bw = BindButtonWidth(lbl);
            if (BindButton(dl, id, R{right - bw, cy, bw, 24.f}, lbl, s.listening == module).clicked && s.listening.empty())
                s.listening = module;
        }
        cy += 24.f + kRowGap;
    };
    keyRow("set:task", "Taskbar Hide", task, "taskbarhide", true);
    if (task) {
        bool hideWindow = task->checks["hideWindow"];
        SwitchRow(dl, "set:taskwin", R{x, cy, iw, 18.f}, "Also Hide The Window", hideWindow);
        task->checks["hideWindow"] = hideWindow;
    }
    cy += 18.f + 20.f;
    keyRow("set:sd", "Self-Destruct Key", sd, "selfdestruct", false);
    if (sd) {
        for (const auto& [key, label] : {std::pair<const char*, const char*>{"closeGame", "Also Close Minecraft"},
                                         {"deleteExe", "Delete CitronLite.exe"}, {"wipeData", "Wipe Citron Lite's Data"}}) {
            char id[48];
            std::snprintf(id, sizeof id, "set:sd:%s", key);
            bool on = sd->checks[key];
            SwitchRow(dl, id, R{x, cy, iw, 18.f}, label, on);
            sd->checks[key] = on;
            cy += 18.f + kRowGap;
        }
    }
    return h;
}

float App::DrawConfigsCard(ImDrawList* dl, float x0, float y, float w, float maxH, float dt) {
    const float x = x0 + 16.f, iw = w - 32.f;
    const float cy = y + 16.f;
    const int n = (int)s.configs.size();
    const float natural = n > 0 ? (float)n * kCfgRowH + (float)(n - 1) * kCfgRowGap : 0.f;
    const float listTop = cy + 29.f + 12.f;
    const float footer = 12.f + 29.f;
    const float listMax = std::max(kCfgRowH, maxH - 16.f - 29.f - 12.f - footer - 16.f);
    const float listH = std::min(natural, listMax);
    const float h = 16.f + 29.f + 12.f + listH + footer + 16.f;
    Fill(dl, R{x0, y, w, h}, Hex(kCardBg), 10.f);
    TextAt(dl, F(600, 14.f), x, cy + (29.f - 19.f) * 0.5f + LM(14.f).asc, Hex(kText), "Configs");
    const float wNew = TextButtonWidth("New config");
    if (TextButton(dl, "cfg:new", R{x + iw - wNew, cy, wNew, 29.f}, "New config", true).clicked) {
        NewConfig();
        cfgScrollTarget = 1e9f;
    }

    const float maxScroll = std::max(0.f, natural - listH);
    cfgScrollTarget = std::clamp(cfgScrollTarget, 0.f, maxScroll);
    cfgScroll = std::clamp(SmoothTo(cfgScroll, cfgScrollTarget, dt), 0.f, maxScroll);
    const R list{x, listTop, iw, listH};
    g_cfgList = R{x0, listTop, w, listH};
    PushClip(list);
    std::string load, remove;
    for (int i = 0; i < n; ++i) {
        const std::string& name = s.configs[(size_t)i];
        const R row{x, listTop - cfgScroll + (float)i * (kCfgRowH + kCfgRowGap), iw, kCfgRowH};
        if (row.y1() < list.y || row.y > list.y1()) continue;
        char id[64];
        const bool confirming = confirmDelete == name;
        const float xw = confirming ? 58.f : 24.f;
        const R xb{row.x1() - 8.f - xw, row.y + 12.f, xw, 24.f}, pb{xb.x - 2.f - 24.f, row.y + 12.f, 24.f, 24.f};
        std::snprintf(id, sizeof id, "cfg:%d:x", i);
        const Hit hx = Item(id, xb);
        Pointer(hx);
        if (hx.clicked) {
            if (confirming) remove = name;
            else confirmDelete = name;
        }
        std::snprintf(id, sizeof id, "cfg:%d:pen", i);
        const Hit hp = Item(id, pb);
        Pointer(hp);
        const ImGuiID renameId = ImHashStr(name.c_str());
        if (hp.clicked) {
            renaming = name;
            BeginEdit(EditKind::Rename, renameId, name);
        }
        const bool editing = EditingTarget(EditKind::Rename, renameId);
        const R field{row.x + 6.f, row.y + 12.f, pb.x - 8.f - (row.x + 6.f), 24.f};
        if (editing) EditFieldHit(field);
        std::snprintf(id, sizeof id, "cfg:%d:row", i);
        const Hit hr = Item(id, row);
        Pointer(hr);
        if (hr.clicked && !editing && name != s.activeConfig) load = name;
        if (hr.hovered && !editing && ImGui::IsMouseDoubleClicked(0)) {
            renaming = name;
            BeginEdit(EditKind::Rename, renameId, name);
        }
        if (!hx.hovered && confirming && ImGui::IsMouseClicked(0)) confirmDelete.clear();
        const bool active = s.activeConfig == name;
        if (hr.hovered || hx.hovered || hp.hovered) Fill(dl, row, White(0.05f), 7.f);
        else if (active) Fill(dl, row, White(0.035f), 7.f);
        std::string committed;
        if (TakeCommit(EditKind::Rename, renameId, committed)) RenameConfig(name, committed);
        if (editing) DrawEditField(dl, field, F(600, 13.f), 6.f);
        else TextAt(dl, F(600, 13.f), row.x + 12.f, row.y + 15.f + LM(13.f).asc, Hex(kText), name.c_str());
        if (active && !editing) TextRight(dl, F(600, 11.f), pb.x - 8.f, row.y + 17.f + LM(11.f).asc, Accent(), "ACTIVE", 0.66f);
        DrawPencil(dl, pb.x + 12.f, pb.y + 12.f, Hex(hp.hovered || editing ? kText : kMuted));
        if (confirming) {
            Fill(dl, xb, Hex(kDanger, hx.hovered ? 0.3f : 0.2f), 6.f);
            TextCenter(dl, F(600, 11.f), xb.x + xb.w * 0.5f, xb.y + 5.f + LM(11.f).asc, Hex(kDanger), "Delete");
        } else {
            DrawCross(dl, xb.x + 12.f, xb.y + 12.f, 4.5f, Hex(hx.hovered ? kDanger : kMuted), 1.5f);
        }
    }
    PopClip();
    ScrollBar(dl, "cfg:scroll", x0 + w - 8.f, listTop + 4.f, listH - 8.f, listH, natural, cfgScroll, cfgScrollTarget);
    // the footer: files
    const float fy = listTop + listH + 12.f;
    const float wImp = TextButtonWidth("Import"), wExp = TextButtonWidth("Export"), wDir = TextButtonWidth("Open folder");
    if (TextButton(dl, "cfg:import", R{x, fy, wImp, 29.f}, "Import", false).clicked) ImportConfig();
    if (TextButton(dl, "cfg:export", R{x + wImp + 8.f, fy, wExp, 29.f}, "Export", false).clicked) ExportConfig();
    if (TextButton(dl, "cfg:folder", R{x + iw - wDir, fy, wDir, 29.f}, "Open folder", false).clicked) cfgio::OpenFolder();
    if (!load.empty()) {
        SaveActive();
        LoadConfig(load);
    }
    if (!remove.empty()) {
        confirmDelete.clear();
        DeleteConfig(remove);
    }
    return h;
}

float App::DrawAboutCard(ImDrawList* dl, float x0, float y, float w) {
    const float h = 16.f + 19.f + 6.f + 16.f + 16.f + (18.f + kRowGap) + (24.f + kRowGap) + (18.f + 6.f + 16.f + 16.f) + 29.f + 16.f;
    Fill(dl, R{x0, y, w, h}, Hex(kCardBg), 10.f);
    const float x = x0 + 16.f, iw = w - 32.f;
    float cy = y + 16.f;
    TextAt(dl, F(600, 14.f), x, cy + LM(14.f).asc, Hex(kText), "Citron Lite");
    TextRight(dl, F(600, 12.f), x + iw, cy + 2.f + LM(12.f).asc, Hex(kMuted), kVersionName);
    cy += 19.f + 6.f;
    TextAt(dl, F(400, 11.5f), x, cy + LM(11.5f).asc, Hex(kMuted), "Free and open source under the GPL-3.0");
    cy += 16.f + 16.f;

    bool updates = updater::Enabled();
    if (SwitchRow(dl, "about:updates", R{x, cy, iw, 18.f}, "Automatic Updates", updates)) updater::SetEnabled(updates);
    cy += 18.f + kRowGap;
    const updater::Status us = updater::Get();
    const char* button = nullptr;
    switch (us.stage) {
    case updater::Stage::Available: button = "Install"; break;
    case updater::Stage::Ready: button = "Restart"; break;
    case updater::Stage::Failed: button = "Try again"; break;
    case updater::Stage::Idle: case updater::Stage::UpToDate: button = "Check now"; break;
    default: break;
    }
    const std::string line = us.detail.empty() ? std::string("Updates come from citron.lol") : us.detail;
    float textW = iw;
    if (button) {
        const float bw = TextButtonWidth(button);
        textW = iw - bw - 12.f;
        const bool primary = us.stage == updater::Stage::Available || us.stage == updater::Stage::Ready;
        if (TextButton(dl, "about:upd", R{x + iw - bw, cy - 2.5f, bw, 29.f}, button, primary).clicked) {
            if (us.stage == updater::Stage::Available) updater::InstallNow();
            else if (us.stage == updater::Stage::Ready) {
                if (updater::RestartToApply()) PostQuitMessage(0);
            } else {
                updater::CheckNow();
            }
        }
    }
    TextAt(dl, F(400, 12.f), x, cy + 3.f + LM(12.f).asc, Hex(kMuted), Ellipsize(F(400, 12.f), line, textW).c_str());
    cy += 24.f + kRowGap;
    bool presence = telemetry::Enabled();
    if (SwitchRow(dl, "about:presence", R{x, cy, iw, 18.f}, "Anonymous Presence", presence)) telemetry::SetEnabled(presence);
    cy += 18.f + 6.f;
    TextAt(dl, F(400, 11.5f), x, cy + LM(11.5f).asc, Hex(kMuted),
           Ellipsize(F(400, 11.5f), "Sends an anonymous ping to the Citron server for telemetry purposes", iw).c_str());
    cy += 16.f + 16.f;

    float bx = x;
    for (const auto& [label, url] : {std::pair<const char*, const wchar_t*>{"Website", L"https://citron.lol"},
                                     {"Discord", L"https://discord.gg/f7JTg86r8A"},
                                     {"GitHub", L"https://github.com/CitronMC/CitronLite"},
                                     {"Ko-fi", L"https://ko-fi.com/center2055"}}) {
        char id[32];
        std::snprintf(id, sizeof id, "about:%s", label);
        const float bw = TextButtonWidth(label);
        if (TextButton(dl, id, R{bx, cy, bw, 29.f}, label, false).clicked) Open(url);
        bx += bw + 8.f;
    }
    return h;
}

float App::DrawSettings(ImDrawList* dl, float x0, float x1, float y, float colW, float dt) {
    float left = DrawThemeCard(dl, x0, y, colW);
    left += 12.f + DrawClientCard(dl, x0, y + left + 12.f, colW);
    const float about = DrawAboutCard(dl, x1, y, colW);
    const float configs = DrawConfigsCard(dl, x1, y + about + 12.f, colW, std::max(260.f, left - about - 12.f), dt);
    return std::max(left, about + 12.f + configs);
}

// The frame.
void App::DrawPanel(const FrameInput& in, FrameOutput& out) {
    BeginFrame(in.scale, in.fontScale, s.accent);
    ImGuiIO& io = ImGui::GetIO();
    const float dt = std::clamp(io.DeltaTime, 0.f, 0.5f);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##citronlite", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // An open editor owns the keyboard; a bind being captured takes the next key or button. A left
    // click is never a bind: it cancels, and the key stays as it was.
    ProcessEditor(io);
    if (!s.listening.empty() && !Editing()) {
        ModuleState* st = s.St(s.listening);
        std::string label = st ? st->key : std::string();
        bool done = ImGui::IsMouseClicked(0);
        static const char* const kMouse[5] = {"", "RMB", "MMB", "Mouse4", "Mouse5"};
        for (int b = 1; b < 5 && !done; ++b)
            if (ImGui::IsMouseClicked(b)) {
                label = kMouse[b];
                done = true;
            }
        if (done) g_interactive = false;
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && !done; ++k) {
            if (!ImGui::IsKeyPressed((ImGuiKey)k, false)) continue;
            if (k == ImGuiKey_Escape) {
                label.clear();
                done = true;
                break;
            }
            const std::string name = ImGuiKeyLabel((ImGuiKey)k);
            if (name.empty()) continue;
            label = name;
            done = true;
        }
        if (done) {
            if (st) st->key = label;
            s.listening.clear();
            justBound = true;
        }
    }

    Fill(dl, R{0, 0, kPanelW, kPanelH}, Hex(kPanelBg), 0.f);
    DrawHeader(dl, out, in.mark);

    const R area{0.f, kHeaderH + 1.f, kPanelW, kPanelH - kHeaderH - 1.f - 8.f};
    const float maxPrev = std::max(0.f, contentHeight - area.h);
    if (io.MouseWheel != 0.f) {
        const float mx = io.MousePos.x / g_s, my = io.MousePos.y / g_s;
        if (s.category == Cat::Settings && g_cfgList.contains(mx, my)) cfgScrollTarget -= io.MouseWheel * 100.f;
        else if (area.contains(mx, my)) scrollTarget -= io.MouseWheel * 100.f;
    }
    scrollTarget = std::clamp(scrollTarget, 0.f, maxPrev);
    scrollY = std::clamp(SmoothTo(scrollY, scrollTarget, dt), 0.f, maxPrev);

    const float tabT = EaseCss((float)((g_now - tabSwitchAt) / 0.2));
    g_alpha = tabT;
    PushClip(area);
    const float colW = (kPanelW - 36.f - 12.f) * 0.5f;
    const float x0 = 18.f, x1 = x0 + colW + 12.f;
    const float y = area.y + 18.f - scrollY + (1.f - tabT) * 10.f;
    float total = 18.f;
    if (s.category != Cat::Settings) {
        g_cfgList = R{};
        std::vector<const ModuleDef*> list;
        for (const ModuleDef& d : s.defs)
            if (d.cat == s.category && !d.hidden) list.push_back(&d);
        // two columns, cards alternating between them, each stacking under the last in its column
        float colY[2] = {y, y};
        for (size_t i = 0; i < list.size(); ++i) {
            const int c = (int)(i & 1);
            ModuleState* st = s.St(list[i]->key);
            if (!st) continue;
            colY[c] += DrawCard(dl, *list[i], *st, c == 0 ? x0 : x1, colY[c], colW) + 12.f;
        }
        total += std::max(colY[0], colY[1]) - y - 12.f;
    } else {
        total += DrawSettings(dl, x0, x1, y, colW, dt);
    }
    total += 18.f;
    PopClip();
    g_alpha = 1.f;
    contentHeight = total;
    ScrollBar(dl, "scroll", area.x1() - 11.f, area.y + 4.f, area.h - 8.f, area.h, total, scrollY, scrollTarget);
    if (ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered() && IsDragHandle(io.MousePos.x / g_s, io.MousePos.y / g_s))
        out.wantDrag = true;
    EndFrameEditor();
    ImGui::SetMouseCursor(g_cursor);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

}  // namespace citron
