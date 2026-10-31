// The panel: a top bar over a page. A module tab is the list on the left and the open module on the
// right. Settings is two columns of cards that always fill the page: both columns share their gutters
// and end on one line, and only the config list scrolls.
//
//   920 x 600, radius 14. Top bar 56: the wordmark at 22, the tabs after it, the game's status, minimise
//   and close on the right. Below it 16 of padding round the page: a list 272 wide, a gap of 12.
#include "citron/app.h"
#include "citron/config_io.h"
#include "citron/telemetry.h"
#include "citron/updater.h"
#include "citron/version.h"
#include "draw.h"
#include "ingame.h"
#include "rows.h"
#include "imgui_internal.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace citron {

using namespace ui;

namespace {

constexpr float kTopBarH = 56.f, kPad = 16.f, kListW = 272.f, kGap = 12.f;
constexpr float kFilterH = 30.f, kRowH = 36.f, kGroupH = 26.f, kGroupGap = 10.f;
constexpr float kDetailPad = 24.f, kDetailHeadH = 84.f, kSetRowH = 34.f, kCheckRowH = 30.f, kLabelW = 148.f;
constexpr float kNameRowH = 34.f;
constexpr float kCardPad = 18.f, kCardHeadH = 42.f, kSwitchRowH = 42.f, kCfgRowH = 46.f;
constexpr int kModuleTabs = 4;

// Settings: three rows of two cards over the page's 528, so every card lines up with the one beside it.
constexpr float kTopRowH = 80.f, kMiddleRowH = kCardHeadH + 5.f * kSwitchRowH + 6.f, kLowerRowH = 166.f;
static_assert(kTopRowH + kGap + kMiddleRowH + kGap + kLowerRowH == kPanelH - kTopBarH - kPad);

struct PanelState {
    std::string selected[kModuleTabs];    // the open module of each module tab
    float listSaved[kModuleTabs] = {};    // where each tab's list was left
    int filter = 0;                       // the list: all, on, bound
    std::vector<std::string> sticky;      // rows that stopped matching the filter while you looked
    float listScroll = 0.f, listTarget = 0.f;
    float detailScroll = 0.f, detailTarget = 0.f;
    float cfgScroll = 0.f, cfgTarget = 0.f;
    double tabChangedAt = -10.0, moduleChangedAt = -10.0, pickerAt = -10.0;
    bool revealSelected = false;
    bool pickerOpen = false;
    R pickerAnchor;
    std::string confirmDelete;
    std::string tip;                      // the status's words, drawn over the page
    R tipAnchor;
    float tabsX0 = 0.f, tabsX1 = 0.f, rightX0 = kPanelW;
    bool wheelTaken = false;
} ps;

void Open(const wchar_t* url) { ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); }

bool WheelOver(const R& area) {
    if (ps.wheelTaken || ImGui::GetIO().MouseWheel == 0.f) return false;
    const ImVec2 m = Mouse();
    if (!area.contains(m.x, m.y)) return false;
    ps.wheelTaken = true;
    return true;
}

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

bool Matches(const AppState& s, const ModuleDef& d, int filter) {
    return filter == 0 || (filter == 1 && RowOn(s, d)) || (filter == 2 && !RowKey(s, d).empty());
}

int ModuleCount(const AppState& s, Cat c, int filter) {
    int n = 0;
    for (const ModuleDef& d : s.defs) n += d.cat == c && !d.hidden && Matches(s, d, filter);
    return n;
}

const ModuleDef* SelectedModule(AppState& s) {
    const int tab = (int)s.category;
    if (tab >= kModuleTabs) return nullptr;
    if (const ModuleDef* d = s.Def(ps.selected[tab]); d && d->cat == s.category && !d->hidden) return d;
    for (const ModuleDef& d : s.defs)
        if (d.cat == s.category && !d.hidden) {
            ps.selected[tab] = d.key;
            return &d;
        }
    return nullptr;
}

void SetTab(AppState& s, Cat c) {
    if (c == s.category) return;
    const int from = (int)s.category, to = (int)c;
    if (from < kModuleTabs) ps.listSaved[from] = ps.listTarget;
    s.category = c;
    ps.tabChangedAt = g_now;
    ps.listScroll = ps.listTarget = to < kModuleTabs ? ps.listSaved[to] : 0.f;
    ps.detailScroll = ps.detailTarget = 0.f;
    ps.sticky.clear();
    ps.confirmDelete.clear();
}

// The list.
void DrawFilter(const AppState& s, const R& r) {
    static const char* const kNames[3] = {"All", "On", "Bound"};
    Fill(r, Hex(tok::kWell), 8.f);
    const float segW = (r.w - 4.f) / 3.f;
    const float hx = Spring(Id("filter-hl"), 2.f + segW * (float)ps.filter, 32.f, 0.86f);
    Fill(R{r.x + hx, r.y + 2.f, segW, r.h - 4.f}, White(0.07f), 6.f);
    for (int i = 0; i < 3; ++i) {
        char id[16];
        std::snprintf(id, sizeof id, "filter%d", i);
        const R b{r.x + 2.f + segW * (float)i, r.y + 2.f, segW, r.h - 4.f};
        const Hit h = Item(id, b);
        PointerOnHover(h);
        if (h.clicked && ps.filter != i) {
            ps.filter = i;
            ps.listTarget = 0.f;
            ps.sticky.clear();
        }
        char num[8];
        std::snprintf(num, sizeof num, "%d", ModuleCount(s, s.category, i));
        const float tw = TextW(Face::Medium, 12.f, kNames[i]), nw = TextW(Face::MonoMedium, 10.5f, num);
        const float x = b.cx() - (tw + 5.f + nw) * 0.5f;
        const float k = Ease(Id(id, 4), ps.filter == i ? 1.f : 0.f, 0.14f);
        TextMid(Face::Medium, 12.f, x, b.cy(), Mix(Hex(h.hovered ? tok::kLabel : tok::kMuted), Hex(tok::kText), k), kNames[i]);
        TextMid(Face::MonoMedium, 10.5f, x + tw + 5.f, b.cy(), Mix(Hex(tok::kFaint), Hex(tok::kSub), k), num);
    }
}

// Lit when on, an empty ring when off.
void StatusLight(const char* key, float cx, float cy, bool on, bool hovered) {
    const float k = Ease(Id(key, 5), on ? 1.f : 0.f, 0.16f);
    const float r = Spring(Id(key, 6), on ? 4.f : 3.5f, 30.f, 0.6f);
    if (k < 1.f) Ring(cx, cy, 3.5f, White(hovered ? 0.45f : 0.22f), 1.4f);
    if (k > 0.f) Circle(cx, cy, r * k + 0.001f, AccentCol());
}

void DrawList(AppState& s, const R& col, float enter) {
    PushAlpha(0.15f + 0.85f * enter);
    DrawFilter(s, R{col.x, col.y, col.w, kFilterH});
    const R area{col.x, col.y + kFilterH + 8.f, col.w, col.h - kFilterH - 8.f};

    // Laid out first, so the scroll range and the selection are known. A row that stops matching
    // while you look at it stays until the tab or the filter changes, so nothing jumps under the pointer.
    struct ListLine { float y; const std::string* group; const ModuleDef* module; };
    std::vector<ListLine> lines;
    float cy = 0.f;
    const std::string* group = nullptr;
    for (const ModuleDef& d : s.defs) {
        if (d.cat != s.category || d.hidden) continue;
        const bool kept = std::find(ps.sticky.begin(), ps.sticky.end(), d.key) != ps.sticky.end();
        if (!Matches(s, d, ps.filter) && !kept) continue;
        if (!group || *group != d.group) {
            if (group) cy += kGroupGap;
            lines.push_back({cy, &d.group, nullptr});
            cy += kGroupH;
            group = &d.group;
        }
        lines.push_back({cy, &d.group, &d});
        cy += kRowH;
    }
    const float content = cy + 6.f;
    const float maxScroll = std::max(0.f, content - area.h);
    const std::string selected = ps.selected[(int)s.category];
    const ImGuiID highlight = Id("list-sel", (int)s.category * 16 + ps.filter);
    if (ps.revealSelected) {
        ps.revealSelected = false;
        for (const ListLine& l : lines) {
            if (!l.module || l.module->key != selected) continue;
            if (l.y < ps.listTarget || l.y + kRowH > ps.listTarget + area.h) ps.listTarget = l.y - kGroupH - kGroupGap;
            SnapSpring(highlight, l.y);
        }
    }
    if (WheelOver(area)) ps.listTarget -= ImGui::GetIO().MouseWheel * 90.f;
    ps.listTarget = std::clamp(ps.listTarget, 0.f, maxScroll);
    ps.listScroll = std::clamp(Approach(ps.listScroll, ps.listTarget), 0.f, maxScroll);
    const ScrollBarState bar = ScrollBarBegin("list-bar", area, ps.listScroll, ps.listTarget, content);

    const float slide = (1.f - enter) * 8.f;
    const float rowW = area.w - (bar.visible ? 12.f : 0.f);   // rows stop short of the scrollbar
    PushClip(area);
    // The highlight springs in list space, so scrolling moves it exactly with its row and only a
    // change of selection animates it.
    for (const ListLine& l : lines) {
        if (!l.module || l.module->key != selected) continue;
        const float hy = Spring(highlight, l.y, 34.f, 0.9f);
        Fill(R{area.x, area.y + hy - ps.listScroll + slide, rowW, kRowH}, Hex(tok::kCard), 8.f);
    }
    for (const ListLine& l : lines) {
        const float y = area.y + l.y - ps.listScroll + slide;
        if (y + kRowH < area.y || y > area.y1()) continue;
        if (!l.module) {
            const float ty = y + kGroupH * 0.5f + 2.f;
            TextMid(Face::SemiBold, 11.f, area.x + 12.f, ty, Hex(tok::kMuted), l.group->c_str());
            const float lx = area.x + 12.f + TextW(Face::SemiBold, 11.f, l.group->c_str()) + 10.f;
            Line(lx, ty, area.x + rowW - 10.f, ty, White(0.05f), 1.f);
            continue;
        }
        const ModuleDef& m = *l.module;
        const R row{area.x, y, rowW, kRowH};
        char id[96];
        // the light first, so it wins the hover over the row
        std::snprintf(id, sizeof id, "light:%s", m.key.c_str());
        const Hit hl = Item(id, R{row.x + 4.f, row.y + 4.f, 26.f, row.h - 8.f});
        PointerOnHover(hl);
        const bool on = RowOn(s, m);
        if (hl.clicked && m.hasToggle) {
            ToggleRow(s, m);
            if (ps.filter != 0) ps.sticky.push_back(m.key);
        }
        std::snprintf(id, sizeof id, "row:%s", m.key.c_str());
        const Hit h = Item(id, row);
        PointerOnHover(h);
        if (h.clicked && selected != m.key) {
            ps.selected[(int)s.category] = m.key;
            ps.moduleChangedAt = g_now;
            ps.detailScroll = ps.detailTarget = 0.f;
        }
        const bool isSel = m.key == selected;
        if (h.hovered && !isSel) Fill(row, White(0.025f), 8.f);
        std::snprintf(id, sizeof id, "led:%s", m.key.c_str());
        if (m.hasToggle) StatusLight(id, row.x + 17.f, row.cy(), on, hl.hovered);
        else DrawIcon(Icon::ChevronRight, row.x + 17.f, row.cy(), 11.f, Hex(tok::kFaint));
        float nameMax = row.w - 32.f - 12.f;
        const std::string key = RowKey(s, m);
        if (!key.empty()) {
            const float kw = KeyCapWidth(key.c_str(), 10.f);
            KeyCap(row.x1() - 10.f - kw, row.cy(), key.c_str(), false, 10.f);
            nameMax -= kw + 8.f;
        }
        const Col nameCol = on ? Hex(tok::kText) : Hex(isSel || h.hovered ? tok::kLabel : tok::kIdle);
        TextMid(Face::Medium, 13.f, row.x + 32.f, row.cy(), nameCol, Ellipsize(Face::Medium, 13.f, m.name, nameMax).c_str());
    }
    if (lines.empty()) {
        const char* msg = ps.filter == 1 ? "Nothing on in this tab" : "Nothing bound in this tab";
        TextMidC(Face::Regular, 12.5f, area.cx(), area.y + 40.f, Hex(tok::kMuted), msg);
    }
    PopClip();
    ScrollBarEnd(bar);
    PopAlpha();
}

// The open module.
float SettingsHeight(const ModuleDef& shown, const ModuleDef& d, const ModuleState& st) {
    float h = (float)(d.sliders.size() + d.choices.size() + (IsClicker(shown) ? 1 : 0)) * kSetRowH;
    if (!d.checks.empty()) h += (h > 0.f ? 12.f : 0.f) + (float)((d.checks.size() + 1) / 2) * kCheckRowH;
    if (d.names) h += (h > 0.f ? 16.f : 0.f) + 32.f + 8.f + (float)st.names.size() * kNameRowH;
    return h;
}

// The list of names: a field to add one, then a row each.
void DrawNames(ModuleState& st, float x, float y, float w) {
    std::string typed;
    if (TextField("friends:add", R{x, y, w, 32.f}, "", "Add a name, or middle click a player in game", typed)) {
        const size_t a = typed.find_first_not_of(' '), b = typed.find_last_not_of(' ');
        const std::string name = a == std::string::npos ? std::string() : typed.substr(a, b - a + 1);
        if (!name.empty() && std::find(st.names.begin(), st.names.end(), name) == st.names.end()) st.names.push_back(name);
    }
    float ry = y + 32.f + 8.f;
    int remove = -1;
    for (size_t i = 0; i < st.names.size(); ++i) {
        const R row{x, ry, w, kNameRowH - 4.f};
        Fill(row, White(0.03f), 7.f);
        TextMid(Face::Regular, 13.f, row.x + 12.f, row.cy(), Hex(tok::kText), Ellipsize(Face::Regular, 13.f, st.names[i], w - 52.f).c_str());
        char id[64];
        std::snprintf(id, sizeof id, "friends:%d:x", (int)i);
        if (IconButton(id, R{row.x1() - 28.f, row.y + 3.f, 24.f, 24.f}, Icon::Close, 13.f, true).clicked) remove = (int)i;
        ry += kNameRowH;
    }
    if (remove >= 0) st.names.erase(st.names.begin() + remove);
}

// Numbers first, then choices, then the checkboxes in two columns.
void DrawModuleSettings(AppState& s, const ModuleDef& shown, const ModuleDef& d, ModuleState& st, float x, float y, float w) {
    char id[160];
    const float y0 = y;
    if (IsClicker(shown)) {
        const R row{x, y, w, kSetRowH};
        static const std::vector<std::string> kSides = {"Left", "Right"};
        const bool lit[2] = {StateOn(s, "clicker"), StateOn(s, "rightclicker")};
        TextMid(Face::Regular, 13.f, row.x, row.cy(), Hex(tok::kLabel), "Side");
        Segmented("clicker-side", row.x1(), row.cy(), kSides, g_clickerSide, lit);
        y += kSetRowH;
    }
    for (const SliderDef& sl : d.sliders) {
        std::snprintf(id, sizeof id, "set:%s:%s", d.key.c_str(), sl.key.c_str());
        const R row{x, y, w, kSetRowH};
        if (sl.range) RangeSlider(id, row, sl.label.c_str(), kLabelW, st.sliders[sl.key + "_min"], st.sliders[sl.key + "_max"], sl.min, sl.max, sl.step, sl.unit.c_str());
        else Slider(id, row, sl.label.c_str(), kLabelW, st.sliders[sl.key], sl.min, sl.max, sl.step, sl.unit.c_str());
        y += kSetRowH;
    }
    for (const ChoiceDef& c : d.choices) {
        std::snprintf(id, sizeof id, "set:%s:%s", d.key.c_str(), c.key.c_str());
        const R row{x, y, w, kSetRowH};
        TextMid(Face::Regular, 13.f, row.x, row.cy(), Hex(tok::kLabel), c.label.c_str());
        int& sel = st.choices[c.key];
        // side by side while they fit beside the label, else one at a time between arrows
        if (c.options.size() <= 4 && SegmentedWidth(c.options) <= w - kLabelW - 24.f) Segmented(id, row.x1(), row.cy(), c.options, sel);
        else ChoiceStepper(id, row.x1(), row.cy(), c.options, sel);
        y += kSetRowH;
    }
    if (!d.checks.empty()) {
        if (y > y0) y += 12.f;
        const float colW = (w - 16.f) * 0.5f;
        for (size_t n = 0; n < d.checks.size(); ++n) {
            const CheckDef& c = d.checks[n];
            std::snprintf(id, sizeof id, "set:%s:%s", d.key.c_str(), c.key.c_str());
            bool on = st.checks.count(c.key) ? st.checks[c.key] : c.def;
            Checkbox(id, x + (float)(n % 2) * (colW + 16.f), y + (float)(n / 2) * kCheckRowH + kCheckRowH * 0.5f, colW, c.label.c_str(), on);
            st.checks[c.key] = on;
        }
        y += (float)((d.checks.size() + 1) / 2) * kCheckRowH;
    }
    if (d.names) DrawNames(st, x, y + (y > y0 ? 16.f : 0.f), w);
}

void DrawDetail(AppState& s, const R& card, float enter) {
    Fill(card, Hex(tok::kCard), 12.f);
    const ModuleDef* mp = SelectedModule(s);
    if (!mp) return;
    const ModuleDef& shown = *mp;
    const ModuleDef& d = SideDef(s, shown);
    ModuleState* stp = s.St(d.key);
    if (!stp) return;
    ModuleState& st = *stp;

    // A new module slides in from a few pixels to the right while it fades up; after a tab change the
    // content fades up with the list. The card itself does not move.
    const float t = std::min(EaseOutT((float)((g_now - ps.moduleChangedAt) / 0.2)), 0.2f + 0.8f * enter);
    PushAlpha(0.2f + 0.8f * t);
    const float dx = (1.f - t) * 6.f;

    // the header: name and description on the left, bind and switch on the right
    const float hx = card.x + kDetailPad + dx, hy = card.y + 22.f;
    char id[128];
    float right = card.x1() - kDetailPad;
    if (d.hasToggle) {
        const R sw{right - 36.f, hy + 4.f, 36.f, 20.f};
        std::snprintf(id, sizeof id, "sw:%s", d.key.c_str());
        Switch(id, sw, st.on);
        right = sw.x - 14.f;
    }
    if (d.bind != BindKind::None) {
        std::snprintf(id, sizeof id, "bind:%s", d.key.c_str());
        const bool capturing = s.listening == d.key;
        const float bw = BindButtonWidth(st.key, capturing);
        if (BindButton(id, right, hy + 14.f, st.key, capturing).clicked && s.listening.empty()) s.listening = d.key;
        right -= bw + 12.f;
    }
    Text(Face::TitleBold, 21.f, hx, hy + 22.f, Hex(tok::kText), Ellipsize(Face::TitleBold, 21.f, shown.name, right - hx).c_str());
    Text(Face::Regular, 12.5f, hx, hy + 44.f, Hex(tok::kMuted), Ellipsize(Face::Regular, 12.5f, d.desc, card.w - 2.f * kDetailPad).c_str());

    // the settings, scrolling under the header
    const R area{card.x, card.y + kDetailHeadH, card.w, card.h - kDetailHeadH - 1.f};
    Line(card.x + kDetailPad, area.y - 0.5f, card.x1() - kDetailPad, area.y - 0.5f, White(0.055f), 1.f);
    const float innerW = card.w - 2.f * kDetailPad;
    const float setH = SettingsHeight(shown, d, st);
    const float content = 20.f + (setH > 0.f ? setH : 20.f) + 20.f;
    const float maxScroll = std::max(0.f, content - area.h);
    if (WheelOver(area)) ps.detailTarget -= ImGui::GetIO().MouseWheel * 90.f;
    ps.detailTarget = std::clamp(ps.detailTarget, 0.f, maxScroll);
    ps.detailScroll = std::clamp(Approach(ps.detailScroll, ps.detailTarget), 0.f, maxScroll);
    const ScrollBarState bar = ScrollBarBegin("detail-bar", R{area.x, area.y, area.w - 4.f, area.h - 6.f}, ps.detailScroll, ps.detailTarget, content);

    PushClip(R{area.x, area.y, area.w, area.h - 6.f});
    const float x = card.x + kDetailPad + dx, y = area.y + 20.f - ps.detailScroll;
    if (setH > 0.f) DrawModuleSettings(s, shown, d, st, x, y, innerW);
    else TextMid(Face::Regular, 12.5f, x, y + 12.f, Hex(tok::kMuted), "No settings. Switch it on and it does its job.");
    PopClip();
    ScrollBarEnd(bar);
    PopAlpha();
}

// Settings.
float CardHead(const R& r, const char* title) {
    char id[48];
    std::snprintf(id, sizeof id, "card:%s", title);
    Note(id, r);
    Fill(r, Hex(tok::kCard), 12.f);
    Text(Face::Title, 15.f, r.x + kCardPad, r.y + 28.f, Hex(tok::kText), title);
    return r.y + kCardHeadH;
}

// Label and description on the left, a switch on the right.
bool SwitchRow(const char* id, float x, float y, float w, const char* label, const char* desc, bool& on) {
    Text(Face::Regular, 13.f, x, y + 17.f, Hex(tok::kText), label);
    Text(Face::Regular, 11.5f, x, y + 33.f, Hex(tok::kMuted), Ellipsize(Face::Regular, 11.5f, desc, w - 60.f).c_str());
    return Switch(id, R{x + w - 30.f, y + kSwitchRowH * 0.5f - 8.5f, 30.f, 17.f}, on);
}

std::string Ago(long long when) {
    if (when <= 0) return "not saved yet";
    const long long d = (long long)std::time(nullptr) - when;
    char b[48];
    if (d < 45) return "saved just now";
    if (d < 3600) std::snprintf(b, sizeof b, "saved %lld min ago", std::max(1LL, d / 60));
    else if (d < 86400) std::snprintf(b, sizeof b, "saved %lld h ago", d / 3600);
    else std::snprintf(b, sizeof b, "saved %lld days ago", d / 86400);
    return b;
}

void DrawAppearance(AppState& s, const R& card) {
    const float y = CardHead(card, "Appearance");
    const float x = card.x + kCardPad, w = card.w - 2.f * kCardPad, cy = y + 11.f, d = 18.f;
    TextMid(Face::Regular, 13.f, x, cy, Hex(tok::kLabel), "Accent");
    // the presets, then one that stands for any colour
    const R custom{x + w - d, cy - d * 0.5f, d, d};
    unsigned accent = s.accent ? s.accent : tok::kLime;
    if (Swatches("accent", custom.x - 8.f, cy, kSwatches, 7, accent, d)) s.accent = accent == tok::kLime ? 0u : accent;
    bool preset = false;
    for (unsigned c : kSwatches) preset |= c == accent;
    const Hit h = Item("accent:custom", R{custom.x - 4.f, cy - 15.f, d + 8.f, 30.f});
    PointerOnHover(h);
    if (h.clicked) {
        ps.pickerOpen = true;
        ps.pickerAt = g_now;
    }
    if (preset) {
        HueRing(custom.cx(), cy, d * 0.5f);
    } else {
        Ring(custom.cx(), cy, d * 0.5f + 3.5f, Hex(tok::kText), 1.5f);
        Circle(custom.cx(), cy, d * 0.5f, Hex(accent));
    }
    ps.pickerAnchor = custom;
}

// Any colour: over the page, so opening it moves nothing. A click outside or Escape puts it away.
void DrawAccentPicker(AppState& s) {
    const float w = 252.f, h = 14.f + PickerHeight() + 14.f + 14.f + 14.f;
    const R card{ps.pickerAnchor.x1() - w, ps.pickerAnchor.y1() + 12.f, w, h};
    const ImVec2 m = Mouse();
    if ((ImGui::IsMouseClicked(0) && !card.contains(m.x, m.y)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ps.pickerOpen = false;
        return;
    }
    PushAlpha(EaseOutT((float)((g_now - ps.pickerAt) / 0.14)));
    Shadow(card, 12.f, 12.f, 32.f, 0.55f);
    Fill(card, Hex(tok::kCard), 12.f);
    Stroke(card, White(0.08f), 12.f, 1.f);
    unsigned rgb = s.accent ? s.accent : tok::kLime;
    if (ColourPicker("accent-pick", card.x + 14.f, card.y + 14.f, card.w - 28.f, rgb)) s.accent = rgb ? rgb : 1u;
    char hex[16];
    std::snprintf(hex, sizeof hex, "#%06X", rgb & 0xFFFFFFu);
    TextMid(Face::MonoMedium, 12.5f, card.x + 14.f, card.y1() - 21.f, Hex(tok::kLabel), hex);
    const R chip{card.x1() - 14.f - 28.f, card.y1() - 21.f - 7.f, 28.f, 14.f};
    Fill(chip, Hex(rgb), 4.f);
    PopAlpha();
}

void DrawInGame(AppState& s, const R& card) {
    const float y = CardHead(card, "In Game");
    const float x = card.x + kCardPad, w = card.w - 2.f * kCardPad, cy = y + 11.f, colW = (w - 24.f) * 0.5f;
    ModuleState* gui = s.St("clickgui");
    ModuleState* notes = s.St("notifications");
    TextMid(Face::Regular, 13.f, x, cy, Hex(tok::kLabel), "Click GUI");
    if (gui && BindButton("set:gui", x + colW, cy, gui->key, s.listening == "clickgui").clicked && s.listening.empty()) s.listening = "clickgui";
    TextMid(Face::Regular, 13.f, x + colW + 24.f, cy, Hex(tok::kLabel), "Notifications");
    if (notes) Switch("set:notes", R{x + w - 30.f, cy - 8.5f, 30.f, 17.f}, notes->on);
}

void DrawClient(AppState& s, const R& card) {
    float y = CardHead(card, "Client");
    const float x = card.x + kCardPad, w = card.w - 2.f * kCardPad;
    ModuleState* stream = s.St("streamhide");
    ModuleState* task = s.St("taskbarhide");
    if (stream) SwitchRow("set:stream", x, y, w, "Stream Hide", "Hidden from screen capture and recordings", stream->on);
    y += kSwitchRowH;
    if (task) {
        SwitchRow("set:task", x, y, w, "Taskbar Hide", "No taskbar button, and its key flips that", task->on);
        if (BindButton("set:task:bind", x + w - 30.f - 12.f, y + kSwitchRowH * 0.5f, task->key, s.listening == "taskbarhide").clicked && s.listening.empty())
            s.listening = "taskbarhide";
    }
    y += kSwitchRowH;
    if (task) {
        bool hideWindow = task->checks["hideWindow"];
        SwitchRow("set:taskwin", x, y, w, "Also Hide The Window", "The window goes too until you press the key", hideWindow);
        task->checks["hideWindow"] = hideWindow;
    }
    y += kSwitchRowH;
    bool updates = updater::Enabled();
    if (SwitchRow("set:updates", x, y, w, "Automatic Updates", "Installs new versions while you are not playing", updates)) updater::SetEnabled(updates);
    y += kSwitchRowH;
    bool presence = telemetry::Enabled();
    if (SwitchRow("set:presence", x, y, w, "Anonymous Presence", "An anonymous ping to the Citron server", presence)) telemetry::SetEnabled(presence);
}

void DrawSelfDestruct(AppState& s, const R& card) {
    const float y = CardHead(card, "Self-Destruct");
    const float x = card.x + kCardPad, w = card.w - 2.f * kCardPad;
    ModuleState* sd = s.St("selfdestruct");
    if (!sd) return;
    const float cy = y + 19.f;
    TextMid(Face::Regular, 13.f, x, cy, Hex(tok::kLabel), "Puts the game back and closes Citron Lite");
    if (BindButton("set:sd", x + w, cy, sd->key, s.listening == "selfdestruct").clicked && s.listening.empty()) s.listening = "selfdestruct";
    const float colW = (w - 16.f) * 0.5f;
    int n = 0;
    for (const auto& [key, label] : {std::pair<const char*, const char*>{"closeGame", "Also Close Minecraft"},
                                     {"deleteExe", "Delete CitronLite.exe"}, {"wipeData", "Wipe Citron Lite's Data"}}) {
        char id[48];
        std::snprintf(id, sizeof id, "set:sd:%s", key);
        bool on = sd->checks[key];
        Checkbox(id, x + (float)(n % 2) * (colW + 16.f), y + 46.f + (float)(n / 2) * kCheckRowH + kCheckRowH * 0.5f, colW, label, on);
        sd->checks[key] = on;
        ++n;
    }
}

// Its rows sit on the lines of the card beside it.
void DrawAbout(const R& card) {
    const float y = CardHead(card, "Citron Lite");
    const float x = card.x + kCardPad, w = card.w - 2.f * kCardPad;
    TextMidR(Face::MonoMedium, 11.f, x + w, card.y + 23.f, Hex(tok::kSub), kVersionName);

    const float cy = y + 19.f;
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
    float textW = w;
    if (button) {
        const float bw = ButtonWidth(button);
        textW = w - bw - 12.f;
        const bool primary = us.stage == updater::Stage::Available || us.stage == updater::Stage::Ready;
        if (Button("about:upd", R{x + w - bw, cy - 14.f, bw, 28.f}, button, primary ? ButtonKind::Primary : ButtonKind::Quiet).clicked) {
            if (us.stage == updater::Stage::Available) updater::InstallNow();
            else if (us.stage == updater::Stage::Ready) {
                if (updater::RestartToApply()) PostQuitMessage(0);
            } else {
                updater::CheckNow();
            }
        }
    }
    TextMid(Face::Regular, 13.f, x, cy, Hex(tok::kLabel), Ellipsize(Face::Regular, 13.f, line, textW).c_str());
    TextMid(Face::Regular, 12.5f, x, y + 61.f, Hex(tok::kMuted), "Free and open source under the GPL-3.0");

    float bx = x;
    for (const auto& [label, url] : {std::pair<const char*, const wchar_t*>{"Website", L"https://citron.lol"},
                                     {"Discord", L"https://discord.gg/f7JTg86r8A"},
                                     {"GitHub", L"https://github.com/CitronMC/CitronLite"},
                                     {"Ko-fi", L"https://ko-fi.com/center2055"}}) {
        char id[32];
        std::snprintf(id, sizeof id, "about:%s", label);
        const float bw = ButtonWidth(label);
        if (Button(id, R{bx, y + 91.f - 14.f, bw, 28.f}, label, ButtonKind::Quiet).clicked) Open(url);
        bx += bw + 6.f;
    }
}

}  // namespace

bool App::IsDragHandle(float x, float y) const {
    if (x < 0.f || x >= kPanelW || y < 0.f || y >= kTopBarH) return false;
    if (x < ps.tabsX0 - 6.f) return true;
    return x > ps.tabsX1 + 4.f && x < ps.rightX0;
}

void App::SelectTab(Cat c) { SetTab(s, c); }

void App::Expand(const std::string& key) {
    const std::string shown = key == "rightclicker" ? "clicker" : key;
    const ModuleDef* d = s.Def(shown);
    if (!d || d->cat == Cat::Settings) return;
    SetTab(s, d->cat);
    ps.selected[(int)d->cat] = shown;
    ps.revealSelected = true;
    if (key == "rightclicker") g_clickerSide = 1;
}

void App::DrawConfigs(const R& card) {
    float y = CardHead(card, "Configs");
    // the header's right: the buttons, then the list has the whole card
    float hx = card.x1() - kCardPad + 10.f;
    auto headButton = [&](const char* id, const char* label) {
        const float bw = ButtonWidth(label);
        hx -= bw;
        const bool clicked = Button(id, R{hx, card.y + 9.f, bw, 28.f}, label, ButtonKind::Ghost).clicked;
        hx -= 2.f;
        return clicked;
    };
    if (headButton("cfg:folder", "Open folder")) cfgio::OpenFolder();
    if (headButton("cfg:export", "Export")) ExportConfig();
    if (headButton("cfg:import", "Import")) ImportConfig();
    if (headButton("cfg:new", "New config")) {
        NewConfig();
        ps.cfgScroll = ps.cfgTarget = 1e6f;
        BeginEdit(EditKind::Rename, Id(s.activeConfig.c_str()), s.activeConfig);
    }

    // what the rows say about the other configs, read every so often
    static std::map<std::string, cfgio::Info> infos;
    static double infoAt = -10.0;
    if (g_now - infoAt > 2.0) {
        infoAt = g_now;
        infos.clear();
        for (const std::string& name : s.configs) infos[name] = cfgio::Peek(name);
    }
    int enabled = 0;
    for (const ModuleDef& d : s.defs) enabled += d.cat != Cat::Settings && StateOn(s, d.key);

    const R list{card.x + 8.f, y, card.w - 16.f, card.y1() - y - 10.f};
    const int n = (int)s.configs.size();
    const float content = (float)n * kCfgRowH;
    const float maxScroll = std::max(0.f, content - list.h);
    if (maxScroll > 0.f && WheelOver(list)) ps.cfgTarget -= ImGui::GetIO().MouseWheel * 70.f;
    ps.cfgTarget = std::clamp(ps.cfgTarget, 0.f, maxScroll);
    ps.cfgScroll = std::clamp(Approach(ps.cfgScroll, ps.cfgTarget), 0.f, maxScroll);
    const ScrollBarState bar = ScrollBarBegin("cfg-bar", list, ps.cfgScroll, ps.cfgTarget, content);
    PushClip(list);
    std::string load, remove;
    const float rowW = list.w - (bar.visible ? 12.f : 0.f);
    for (int i = 0; i < n; ++i) {
        const std::string name = s.configs[(size_t)i];
        const R row{list.x, list.y + (float)i * kCfgRowH - ps.cfgScroll, rowW, kCfgRowH - 2.f};
        if (row.y1() < list.y || row.y > list.y1()) continue;
        const bool active = name == s.activeConfig, confirming = ps.confirmDelete == name;
        const ImGuiID renameId = Id(name.c_str());
        char id[96];
        // the actions first, so they win the hover over the row
        float ax = row.x1() - 8.f;
        std::snprintf(id, sizeof id, "cfg:%s:x", name.c_str());
        Hit hx2;
        if (confirming) {
            const float dw = ButtonWidth("Delete");
            hx2 = Button(id, R{ax - dw, row.cy() - 14.f, dw, 28.f}, "Delete", ButtonKind::Danger);
            ax -= dw + 2.f;
        } else {
            hx2 = IconButton(id, R{ax - 28.f, row.cy() - 14.f, 28.f, 28.f}, Icon::Trash, 15.f, true);
            ax -= 30.f;
        }
        if (hx2.clicked) {
            if (confirming) remove = name;
            else ps.confirmDelete = name;
        } else if (confirming && ImGui::IsMouseClicked(0) && !hx2.hovered) {
            ps.confirmDelete.clear();
        }
        std::snprintf(id, sizeof id, "cfg:%s:pen", name.c_str());
        if (IconButton(id, R{ax - 28.f, row.cy() - 14.f, 28.f, 28.f}, Icon::Pencil, 15.f).clicked) BeginEdit(EditKind::Rename, renameId, name);
        ax -= 30.f;
        std::string committed;
        if (TakeCommit(EditKind::Rename, renameId, committed)) RenameConfig(name, committed);
        const bool editing = EditingTarget(EditKind::Rename, renameId);
        std::snprintf(id, sizeof id, "cfg:%s:row", name.c_str());
        const Hit h = editing ? Hit{} : Item(id, row);
        PointerOnHover(h);
        if (h.clicked && !active) load = name;
        if (h.hovered && ImGui::IsMouseDoubleClicked(0)) BeginEdit(EditKind::Rename, renameId, name);
        if (active) Fill(row, White(0.045f), 8.f);
        else if (h.hovered) Fill(row, White(0.025f), 8.f);
        if (active) Circle(row.x + 16.f, row.cy(), 3.5f, AccentCol());
        else Ring(row.x + 16.f, row.cy(), 3.f, White(0.22f), 1.3f);
        if (editing) {
            EditField(R{row.x + 28.f, row.y + 7.f, ax - row.x - 34.f, 30.f}, Face::Medium, 13.f, 10.f);
            continue;
        }
        const float textW = ax - row.x - 36.f;
        TextMid(Face::Medium, 13.f, row.x + 30.f, row.cy() - 7.5f, Hex(active ? tok::kText : tok::kLabel), Ellipsize(Face::Medium, 13.f, name, textW).c_str());
        char meta[96];
        if (active) std::snprintf(meta, sizeof meta, "in use, %d on, saves as you go", enabled);
        else std::snprintf(meta, sizeof meta, "%d on, %s", infos[name].enabled, Ago(infos[name].savedAt).c_str());
        TextMid(Face::Regular, 11.5f, row.x + 30.f, row.cy() + 8.5f, Hex(tok::kMuted), Ellipsize(Face::Regular, 11.5f, meta, textW).c_str());
    }
    PopClip();
    ScrollBarEnd(bar);
    if (!load.empty()) {
        SaveActive();
        LoadConfig(load);
    }
    if (!remove.empty()) {
        ps.confirmDelete.clear();
        DeleteConfig(remove);
        infoAt = -10.0;
    }
}

void App::DrawSettings(const R& page) {
    const float colW = (page.w - kGap) * 0.5f, rightX = page.x + colW + kGap;
    const float midY = page.y + kTopRowH + kGap, lowY = midY + kMiddleRowH + kGap;
    DrawAppearance(s, R{page.x, page.y, colW, kTopRowH});
    DrawInGame(s, R{rightX, page.y, colW, kTopRowH});
    DrawClient(s, R{page.x, midY, colW, kMiddleRowH});
    DrawConfigs(R{rightX, midY, colW, kMiddleRowH});
    DrawSelfDestruct(s, R{page.x, lowY, colW, kLowerRowH});
    DrawAbout(R{rightX, lowY, colW, kLowerRowH});
}

// The game, once it has been found; nothing while Minecraft is not running. Returns where it starts.
float App::DrawStatus(float x1, float cy) {
    const BackendStatus st = backend.Status();
    if (st.kind == BackendStatus::Kind::Searching) return x1;
    std::string text;
    bool problem = false;
    if (st.kind == BackendStatus::Kind::Unsupported) {
        text = "Unsupported version";
        problem = true;
    } else {
        const size_t cut = st.version.rfind('.');
        text = "Minecraft " + (cut != std::string::npos ? st.version.substr(0, cut) : st.version);
        problem = st.kind == BackendStatus::Kind::Partial;
    }
    const float w = 26.f + TextW(Face::Medium, 12.f, text.c_str()) + 12.f;
    const R pill{x1 - w, cy - 15.f, w, 30.f};
    const Hit h = Item("status", pill);
    Fill(pill, Hex(tok::kCard), 9.f);
    Stroke(pill, problem ? Hex(tok::kAmber, 0.24f) : AccentCol(0.24f), 9.f, 1.f);
    Circle(pill.x + 14.f, cy, 3.5f, problem ? Hex(tok::kAmber) : AccentCol());
    TextMid(Face::Medium, 12.f, pill.x + 26.f, cy, Hex(tok::kLabel), text.c_str());
    if (h.hovered && (!st.detail.empty() || st.kind == BackendStatus::Kind::Unsupported)) {
        ps.tip = st.detail.empty() ? std::string("Citron Lite runs on 1.26.52 and 1.26.60") : st.detail;
        ps.tipAnchor = pill;
    }
    return pill.x;
}

void App::DrawTopBar(const FrameInput& in, FrameOutput& out) {
    const float cy = kTopBarH * 0.5f;
    // the wordmark, its leaves carrying the accent, and a small tag on its baseline
    const float h = 19.f;
    const R mark{22.f, cy + 10.5f - h, h * in.logoAspect, h};
    if (in.letters) DL()->AddImage(in.letters, Min(mark), Max(mark), ImVec2(0, 0), ImVec2(1, 1), Hex(tok::kText));
    if (in.leaves) DL()->AddImage(in.leaves, Min(mark), Max(mark), ImVec2(0, 0), ImVec2(1, 1), AccentCol());
    const R tag{mark.x1() + 6.f, mark.y1() - 14.f, TextW(Face::MonoMedium, 10.5f, "Lite") + 9.f, 14.f};
    Fill(tag, AccentCol(0.16f), 4.f);
    TextMidC(Face::MonoMedium, 10.5f, tag.cx(), tag.cy(), AccentCol(), "Lite");

    float tx = tag.x1() + 22.f;
    ps.tabsX0 = tx;
    for (const CatInfo& c : kCats) {
        const int count = c.cat == Cat::Settings ? 0 : ModuleCount(s, c.cat, 1);
        char num[8], id[32];
        std::snprintf(num, sizeof num, "%d", count);
        std::snprintf(id, sizeof id, "tab:%s", c.key);
        const float textW = TextW(Face::Medium, 13.f, c.label);
        const float countW = count > 0 ? 6.f + TextW(Face::MonoMedium, 10.5f, num) : 0.f;
        // the width springs when a count appears, so the tabs to the right glide over
        const R b{tx, 12.f, Spring(Id(id, 3), 24.f + textW + countW, 30.f, 1.f), 32.f};
        const Hit hit = Item(id, b);
        PointerOnHover(hit);
        if (hit.clicked) SetTab(s, c.cat);
        const bool active = s.category == c.cat;
        const float bg = Ease(Id(id, 1), active ? 1.f : (hit.hovered ? 0.45f : 0.f), 0.16f);
        Fill(b, White(0.06f * bg), 8.f);
        const float k = Ease(Id(id, 2), active ? 1.f : (hit.hovered ? 0.6f : 0.f), 0.16f);
        TextMid(Face::Medium, 13.f, b.x + 12.f, b.cy(), Mix(Hex(tok::kMuted), Hex(tok::kText), k), c.label);
        if (count > 0) TextMid(Face::MonoMedium, 10.5f, b.x + 12.f + textW + 6.f, b.cy(), AccentCol(active ? 1.f : 0.7f), num);
        tx = b.x1() + 2.f;
    }
    ps.tabsX1 = tx;

    const R close{kPanelW - 12.f - 32.f, 12.f, 32.f, 32.f}, mini{close.x - 2.f - 32.f, 12.f, 32.f, 32.f};
    if (IconButton("close", close, Icon::Close, 15.f).clicked) out.wantClose = true;
    if (IconButton("minimize", mini, Icon::Minimize, 15.f).clicked) out.wantMinimize = true;
    const float rule = mini.x - 10.f, statusX = DrawStatus(rule - 10.f, cy);
    if (statusX < rule - 10.f) Line(rule, cy - 8.f, rule, cy + 8.f, White(0.08f), 1.f);
    ps.rightX0 = statusX - 8.f;
}

void App::DrawPanel(const FrameInput& in, FrameOutput& out) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##citronlite", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    BeginFrame(ImGui::GetWindowDrawList(), in.scale, in.fontScale, s.accent);
    SetIconTexture(in.icons);

    // An open editor owns the keyboard; a bind being captured takes the next key or button. A left
    // click is never a bind: it cancels, and the key stays as it was.
    ProcessEditor(io);
    bool pointerOk = true;
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
        if (done) pointerOk = false;
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

    if (previewGui) {
        const R view{0.f, 0.f, io.DisplaySize.x / in.scale, io.DisplaySize.y / in.scale};
        Fill(view, Hex(0x5a7388));
        DrawClickGui(s, view, 1.f);
        const unsigned long long now = GetTickCount64();
        const std::vector<Toast> sample = {{"Aim Assist", "On", true, now - 600}, {"Reach", "Off", false, now - 600}, {"Steve", "Added", true, now - 600}};
        DrawToasts(sample, view, now);
        ImGui::SetMouseCursor(FrameCursor());
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    ps.wheelTaken = false;
    ps.tip.clear();
    const R panel{0.f, 0.f, kPanelW, kPanelH};
    Fill(panel, Hex(tok::kPanel));
    Stroke(panel, White(0.05f), kPanelRadius, 1.f);   // on the panel's own edge: the window ends there

    SetInteractive(pointerOk && !ps.pickerOpen);
    DrawTopBar(in, out);

    // After a tab change the content fades up; the list and the card's surface stay put.
    const R body{kPad, kTopBarH, kPanelW - 2.f * kPad, kPanelH - kTopBarH - kPad};
    const float t = EaseOutT((float)((g_now - ps.tabChangedAt) / 0.22));
    if (s.category == Cat::Settings) {
        PushAlpha(0.15f + 0.85f * t);
        DrawSettings(R{body.x, body.y + (1.f - t) * 8.f, body.w, body.h});
        PopAlpha();
    } else {
        DrawList(s, R{body.x, body.y, kListW, body.h}, t);
        DrawDetail(s, R{body.x + kListW + kGap, body.y, body.w - kListW - kGap, body.h}, t);
    }

    SetInteractive(pointerOk);
    if (ps.pickerOpen) {
        if (s.category == Cat::Settings) DrawAccentPicker(s);
        else ps.pickerOpen = false;
    }
    if (!ps.tip.empty()) {
        const float tw = TextW(Face::Regular, 12.f, ps.tip.c_str()) + 20.f;
        const R box{std::max(8.f, ps.tipAnchor.x1() - tw), ps.tipAnchor.y1() + 6.f, tw, 26.f};
        Shadow(box, 7.f, 4.f, 12.f, 0.4f);
        Fill(box, Hex(tok::kCardHover), 7.f);
        Stroke(box, White(0.08f), 7.f, 1.f);
        TextMid(Face::Regular, 12.f, box.x + 10.f, box.cy(), Hex(tok::kText), ps.tip.c_str());
    }

    if (ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered() && IsDragHandle(io.MousePos.x / g_s, io.MousePos.y / g_s)) out.wantDrag = true;
    EndFrameEditor();
    ImGui::SetMouseCursor(FrameCursor());
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void App::OverlayFrame(const FrameInput& in) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##citronlite-overlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    BeginFrame(ImGui::GetWindowDrawList(), in.scale, in.fontScale, s.accent, 1);
    SetIconTexture(in.icons);
    SetInteractive(gui == Gui::Open);
    const R view{0.f, 0.f, io.DisplaySize.x / in.scale, io.DisplaySize.y / in.scale};
    const unsigned long long now = GetTickCount64();
    if (gui != Gui::Closed) DrawClickGui(s, view, EaseOutT((float)(now - guiAt) / 160.f));
    DrawToasts(toasts, view, now);
    ImGui::SetMouseCursor(FrameCursor());
    ImGui::End();
    ImGui::PopStyleVar(2);
}

}  // namespace citron
