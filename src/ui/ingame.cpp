// What is drawn over the game: the Click GUI and the notifications.
//
// The Click GUI is a column for each tab. A click on a row switches the module; its arrow, or a right
// click, opens the module's settings under the row. Everything is on one screen, and the settings are
// stacked to fit a column.
#include "ingame.h"
#include "draw.h"
#include "rows.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace citron {

using namespace ui;

namespace {

constexpr float kColW = 216.f, kColGap = 12.f, kHeadH = 42.f, kRowH = 34.f, kInset = 14.f;
constexpr float kLineH = 34.f, kStackH = 42.f, kCheckH = 28.f, kNameH = 30.f;
constexpr int kColumns = 4;

struct GuiState {
    std::string open;                // the module whose settings are out
    float scroll[kColumns] = {};
    float target[kColumns] = {};
} gs;

bool FitsBeside(const ChoiceDef& c, float w) { return c.options.size() <= 4 && SegmentedWidth(c.options) + 8.f + TextW(Face::Regular, 12.5f, c.label.c_str()) <= w; }

float SettingsHeight(const ModuleDef& shown, const ModuleDef& d, const ModuleState& st, float w) {
    float h = 4.f;
    if (IsClicker(shown)) h += kLineH;
    if (d.bind != BindKind::None) h += kLineH;
    h += (float)d.sliders.size() * kStackH;
    for (const ChoiceDef& c : d.choices) h += FitsBeside(c, w) ? kLineH : kLineH + 22.f;
    h += (float)d.checks.size() * kCheckH;
    if (d.names) h += (float)st.names.size() * kNameH;
    return h + 8.f;
}

void DrawSettings(AppState& s, const ModuleDef& shown, const ModuleDef& d, ModuleState& st, float x, float y, float w) {
    char id[128];
    y += 4.f;
    if (IsClicker(shown)) {
        static const std::vector<std::string> kSides = {"Left", "Right"};
        const bool lit[2] = {StateOn(s, "clicker"), StateOn(s, "rightclicker")};
        TextMid(Face::Regular, 12.5f, x, y + kLineH * 0.5f, Hex(tok::kLabel), "Side");
        Segmented("gui:side", x + w, y + kLineH * 0.5f, kSides, g_clickerSide, lit);
        y += kLineH;
    }
    if (d.bind != BindKind::None) {
        std::snprintf(id, sizeof id, "gui:bind:%s", d.key.c_str());
        TextMid(Face::Regular, 12.5f, x, y + kLineH * 0.5f, Hex(tok::kLabel), "Key");
        if (BindButton(id, x + w, y + kLineH * 0.5f, st.key, s.listening == d.key).clicked && s.listening.empty()) s.listening = d.key;
        y += kLineH;
    }
    for (const SliderDef& sl : d.sliders) {
        std::snprintf(id, sizeof id, "gui:set:%s:%s", d.key.c_str(), sl.key.c_str());
        const R row{x, y, w, kStackH};
        if (sl.range) StackRange(id, row, sl.label.c_str(), st.sliders[sl.key + "_min"], st.sliders[sl.key + "_max"], sl.min, sl.max, sl.step, sl.unit.c_str());
        else StackSlider(id, row, sl.label.c_str(), st.sliders[sl.key], sl.min, sl.max, sl.step, sl.unit.c_str());
        y += kStackH;
    }
    for (const ChoiceDef& c : d.choices) {
        std::snprintf(id, sizeof id, "gui:set:%s:%s", d.key.c_str(), c.key.c_str());
        int& sel = st.choices[c.key];
        if (FitsBeside(c, w)) {
            TextMid(Face::Regular, 12.5f, x, y + kLineH * 0.5f, Hex(tok::kLabel), c.label.c_str());
            Segmented(id, x + w, y + kLineH * 0.5f, c.options, sel);
            y += kLineH;
        } else {
            TextMid(Face::Regular, 12.5f, x, y + 11.f, Hex(tok::kLabel), c.label.c_str());
            ChoiceStepper(id, x + w, y + 22.f + kLineH * 0.5f, c.options, sel);
            y += kLineH + 22.f;
        }
    }
    for (const CheckDef& c : d.checks) {
        std::snprintf(id, sizeof id, "gui:set:%s:%s", d.key.c_str(), c.key.c_str());
        bool on = st.checks.count(c.key) ? st.checks[c.key] : c.def;
        Checkbox(id, x, y + kCheckH * 0.5f, w, c.label.c_str(), on);
        st.checks[c.key] = on;
        y += kCheckH;
    }
    if (d.names) {
        int remove = -1;
        for (size_t i = 0; i < st.names.size(); ++i) {
            const R row{x, y, w, kNameH - 4.f};
            Fill(row, White(0.03f), 7.f);
            TextMid(Face::Regular, 12.5f, row.x + 10.f, row.cy(), Hex(tok::kText), Ellipsize(Face::Regular, 12.5f, st.names[i], w - 44.f).c_str());
            std::snprintf(id, sizeof id, "gui:friend:%d", (int)i);
            if (IconButton(id, R{row.x1() - 25.f, row.y + 1.f, 24.f, 24.f}, Icon::Close, 12.f, true).clicked) remove = (int)i;
            y += kNameH;
        }
        if (remove >= 0) st.names.erase(st.names.begin() + remove);
    }
}

void DrawColumn(AppState& s, const CatInfo& cat, int index, const R& col, float maxH) {
    std::vector<const ModuleDef*> mods;
    int lit = 0;
    for (const ModuleDef& d : s.defs)
        if (d.cat == cat.cat && !d.hidden) {
            mods.push_back(&d);
            lit += RowOn(s, d);
        }

    // how tall each row's settings stand right now, so the card can follow them
    float content = 0.f;
    std::vector<float> extra(mods.size());
    char id[128];
    for (size_t i = 0; i < mods.size(); ++i) {
        const ModuleDef& m = *mods[i];
        const ModuleDef& d = SideDef(s, m);
        const ModuleState* st = s.St(d.key);
        std::snprintf(id, sizeof id, "gui:h:%s", m.key.c_str());
        const float full = st ? SettingsHeight(m, d, *st, col.w - 2.f * kInset) : 0.f;
        extra[i] = full * Ease(Id(id), gs.open == m.key ? 1.f : 0.f, 0.16f);
        content += kRowH + extra[i];
    }
    content += 6.f;

    const float listH = std::min(content, maxH - kHeadH);
    const R card{col.x, col.y, col.w, kHeadH + listH};
    Note((std::string("gui:col:") + cat.key).c_str(), card);
    Shadow(card, 12.f, 8.f, 24.f, 0.35f);
    Fill(card, Hex(tok::kPanel, 0.97f), 12.f);
    Stroke(card, White(0.05f), 12.f, 1.f);
    TextMid(Face::Title, 15.f, card.x + kInset + 2.f, card.y + 23.f, Hex(tok::kText), cat.label);
    if (lit > 0) {
        char num[8];
        std::snprintf(num, sizeof num, "%d", lit);
        TextMidR(Face::MonoMedium, 10.5f, card.x1() - kInset - 2.f, card.y + 23.f, AccentCol(), num);
    }

    const R list{card.x, card.y + kHeadH, card.w, listH};
    const float maxScroll = std::max(0.f, content - list.h);
    float& scroll = gs.scroll[index];
    float& target = gs.target[index];
    if (ImGui::GetIO().MouseWheel != 0.f && list.contains(Mouse().x, Mouse().y)) target -= ImGui::GetIO().MouseWheel * 70.f;
    target = std::clamp(target, 0.f, maxScroll);
    scroll = std::clamp(Approach(scroll, target), 0.f, maxScroll);
    std::snprintf(id, sizeof id, "gui:bar:%s", cat.key);
    const ScrollBarState bar = ScrollBarBegin(id, list, scroll, target, content);

    PushClip(list);
    float y = list.y - scroll;
    const float rowW = card.w - 12.f - (bar.visible ? 6.f : 0.f);
    for (size_t i = 0; i < mods.size(); ++i) {
        const ModuleDef& m = *mods[i];
        const bool open = gs.open == m.key;
        const R row{card.x + 6.f, y, rowW, kRowH};
        if (y + kRowH + extra[i] >= list.y && y <= list.y1()) {
            // the arrow first, so it wins the hover over the row
            std::snprintf(id, sizeof id, "gui:more:%s", m.key.c_str());
            const R arrow{row.x1() - 28.f, row.y + 3.f, 26.f, row.h - 6.f};
            const Hit ha = Item(id, arrow);
            PointerOnHover(ha);
            std::snprintf(id, sizeof id, "gui:row:%s", m.key.c_str());
            const Hit h = Item(id, row);
            PointerOnHover(h);
            const bool on = RowOn(s, m);
            if (h.clicked && m.hasToggle) ToggleRow(s, m);
            const bool asked = ha.clicked || ((h.hovered || ha.hovered) && ImGui::IsMouseClicked(1));
            if (asked) gs.open = open ? std::string() : m.key;
            if (open) Fill(row, White(0.04f), 8.f);
            else if (h.hovered || ha.hovered) Fill(row, White(0.03f), 8.f);
            const float lightK = Ease(Id(id, 5), on ? 1.f : 0.f, 0.16f);
            const float cx = row.x + 14.f, cy = row.cy();
            if (lightK < 1.f) Ring(cx, cy, 3.5f, White(h.hovered ? 0.45f : 0.22f), 1.4f);
            if (lightK > 0.f) Circle(cx, cy, 4.f * lightK + 0.001f, AccentCol());
            float nameMax = row.w - 28.f - 30.f;
            const std::string key = RowKey(s, m);
            float right = arrow.x;
            if (!key.empty()) {
                const float kw = KeyCapWidth(key.c_str(), 10.f);
                right -= kw + 2.f;
                KeyCap(right, cy, key.c_str(), false, 10.f);
                nameMax -= kw + 6.f;
            }
            TextMid(Face::Medium, 13.f, row.x + 28.f, cy, Hex(on ? tok::kText : h.hovered ? tok::kLabel : tok::kIdle), Ellipsize(Face::Medium, 13.f, m.name, nameMax).c_str());
            DrawIcon(open ? Icon::ChevronDown : Icon::ChevronRight, arrow.cx(), arrow.cy(), 13.f, Hex(ha.hovered ? tok::kText : tok::kFaint));
            if (extra[i] > 0.5f) {
                const ModuleDef& d = SideDef(s, m);
                if (ModuleState* st = s.St(d.key)) {
                    const R area{card.x, y + kRowH, card.w, extra[i]};
                    PushClip(R{area.x, std::max(area.y, list.y), area.w, std::min(area.y1(), list.y1()) - std::max(area.y, list.y)});
                    DrawSettings(s, m, d, *st, card.x + kInset + 2.f, area.y, card.w - 2.f * kInset - 4.f - (bar.visible ? 6.f : 0.f));
                    PopClip();
                }
            }
        }
        y += kRowH + extra[i];
    }
    PopClip();
    ScrollBarEnd(bar);
}

}  // namespace

void DrawClickGui(AppState& s, const R& view, float t) {
    Fill(view, Black(0.3f * t));
    PushAlpha(t);
    std::vector<const CatInfo*> cats;
    for (const CatInfo& c : kCats)
        if (c.cat != Cat::Settings) cats.push_back(&c);
    const float total = (float)cats.size() * kColW + (float)(cats.size() - 1) * kColGap;
    const float top = view.y + std::clamp(view.h * 0.13f, 16.f, 150.f) + (1.f - t) * 10.f;
    float x = view.cx() - total * 0.5f;
    for (size_t i = 0; i < cats.size(); ++i) {
        DrawColumn(s, *cats[i], (int)i, R{x, top, kColW, 0.f}, view.y1() - top - 16.f);
        x += kColW + kColGap;
    }
    PopAlpha();
}

void DrawToasts(const std::vector<Toast>& toasts, const R& view, unsigned long long nowMs) {
    float y = view.y1() - 20.f;
    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
        const float age = (float)(nowMs - it->atMs) / 1000.f, left = kToastSeconds - age;
        const float in = EaseOutT(age / 0.18f), out = std::clamp(left / 0.25f, 0.f, 1.f);
        const float titleW = TextW(Face::Medium, 13.f, it->title.c_str()), stateW = TextW(Face::MonoMedium, 11.f, it->state.c_str());
        const float w = 30.f + titleW + 16.f + stateW + 14.f, h = 36.f;
        const R card{view.x1() - 20.f - w + (1.f - in) * 18.f, y - h, w, h};
        PushAlpha(std::min(in, out));
        Shadow(card, 10.f, 6.f, 18.f, 0.35f);
        Fill(card, Hex(tok::kPanel, 0.96f), 10.f);
        Stroke(card, White(0.06f), 10.f, 1.f);
        if (it->lit) Circle(card.x + 16.f, card.cy(), 4.f, AccentCol());
        else Ring(card.x + 16.f, card.cy(), 3.5f, White(0.3f), 1.4f);
        TextMid(Face::Medium, 13.f, card.x + 30.f, card.cy(), Hex(tok::kText), it->title.c_str());
        TextMidR(Face::MonoMedium, 11.f, card.x1() - 14.f, card.cy(), it->lit ? AccentCol() : Hex(tok::kSub), it->state.c_str());
        PopAlpha();
        y -= (h + 8.f) * std::min(in, 1.f);
    }
}

}  // namespace citron
