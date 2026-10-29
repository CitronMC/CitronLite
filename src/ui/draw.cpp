#include "draw.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace citron::ui {

const unsigned kSwatches[6] = {0xc8f135, 0xfbbf24, 0x34e0a1, 0x38e1ff, 0xb48bff, 0xff6fae};

unsigned g_accent = kDefaultAccent;
float g_alpha = 1.f;
float g_s = 1.f, g_fk = 1.f;
double g_now = 0.0;
bool g_interactive = true;
ImGuiMouseCursor g_cursor = ImGuiMouseCursor_Arrow;

namespace {
bool g_lightAccent = false;

float Luminance(unsigned rgb) {
    auto lin = [](float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(((rgb >> 16) & 255) / 255.f) + 0.7152f * lin(((rgb >> 8) & 255) / 255.f) +
           0.0722f * lin((rgb & 255) / 255.f);
}
}  // namespace

ImU32 Hex(unsigned rgb, float a) {
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (int)(a * g_alpha * 255.f + 0.5f));
}
ImU32 White(float a) { return Hex(0xffffff, a); }
ImU32 Black(float a) { return Hex(0x000000, a); }
ImU32 Accent(float a) { return Hex(g_accent, a); }
bool LightAccent() { return g_lightAccent; }
// Text and knob rims on an accent surface: dark on a light accent, light on a dark one.
ImU32 OnAccent() { return g_lightAccent ? Hex(kPanelBg) : Hex(kText); }

// Colours mix in premultiplied space, the way a browser transitions them.
ImU32 Mix(ImU32 a, ImU32 b, float t) {
    const float aa = ((a >> IM_COL32_A_SHIFT) & 255) / 255.f, ab = ((b >> IM_COL32_A_SHIFT) & 255) / 255.f;
    const float al = aa + (ab - aa) * t;
    if (al <= 0.0001f) return IM_COL32(0, 0, 0, 0);
    auto ch = [&](int sh) {
        const float ca = ((a >> sh) & 255) / 255.f, cb = ((b >> sh) & 255) / 255.f;
        return (ca * aa * (1.f - t) + cb * ab * t) / al;
    };
    return ImGui::ColorConvertFloat4ToU32(ImVec4(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), al));
}

ImU32 Brighten(ImU32 c, float k) {
    auto ch = [&](int sh) {
        const int v = (int)(((c >> sh) & 255) * k + 0.5f);
        return v > 255 ? 255 : v;
    };
    return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), (c >> IM_COL32_A_SHIFT) & 255);
}

unsigned HsvToRgb(float h, float s, float v) {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return ((unsigned)(r * 255.f + 0.5f) << 16) | ((unsigned)(g * 255.f + 0.5f) << 8) | (unsigned)(b * 255.f + 0.5f);
}

// Fonts.
// Albert Sans: 1000 units to the em, ascender 950, descender -250. ImGui sizes a font by the sum,
// so a design size S becomes S * 1.2. The layout's line boxes are 1.066 em above the baseline and
// 0.3 em below, with the font's own box centred in them.
namespace {
constexpr float kAscEm = 0.95f, kDescEm = 0.25f, kImGuiSizeEm = 1.2f;
constexpr float kBoxAscEm = 2132.f / 2000.f, kBoxDescEm = 600.f / 2000.f;
struct FontSlot { int weight; float size; ImFont* font; };
FontSlot g_fonts[] = {
    {400, 11.f, nullptr},  {400, 11.5f, nullptr}, {400, 12.f, nullptr},  {400, 13.f, nullptr}, {400, 14.f, nullptr},
    {600, 10.5f, nullptr}, {600, 11.f, nullptr},  {600, 11.5f, nullptr}, {600, 12.f, nullptr}, {600, 13.f, nullptr},
    {600, 14.f, nullptr},  {700, 15.f, nullptr},  {700, 17.f, nullptr},
};
float RoundHalfUp(float v) { return floorf(v + 0.5f); }
}  // namespace

void BuildFonts(ImFontAtlas* atlas, FontData regular, FontData semibold, FontData bold, float scale) {
    atlas->Clear();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2026, 0x2026, 0};
    for (auto& s : g_fonts) {
        const FontData& fd = s.weight == 700 ? bold : s.weight == 600 ? semibold : regular;
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        cfg.OversampleH = 3;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = false;
        std::snprintf(cfg.Name, sizeof cfg.Name, "Albert Sans %d %.1f", s.weight, s.size);
        s.font = atlas->AddFontFromMemoryTTF(const_cast<void*>(fd.data), fd.size, s.size * kImGuiSizeEm * scale, &cfg, ranges);
    }
}

ImFont* F(int weight, float size) {
    for (auto& s : g_fonts)
        if (s.weight == weight && fabsf(s.size - size) < 0.01f) return s.font;
    IM_ASSERT(0 && "font not in g_fonts");
    return g_fonts[0].font;
}

LineM LM(float size) {
    LineM m;
    m.height = RoundHalfUp(kBoxAscEm * size) + RoundHalfUp(kBoxDescEm * size);
    m.asc = (m.height - (kAscEm + kDescEm) * size) * 0.5f + kAscEm * size;
    m.desc = m.height - m.asc;
    return m;
}

// Geometry.
float Dv(float v) { return v * g_s; }
static float Snap(float v) { return floorf(v * g_s + 0.5f); }
ImVec2 Min(const R& r) { return ImVec2(Snap(r.x), Snap(r.y)); }
ImVec2 Max(const R& r) { return ImVec2(Snap(r.x1()), Snap(r.y1())); }

void Fill(ImDrawList* dl, const R& r, ImU32 col, float rounding) {
    if (((col >> IM_COL32_A_SHIFT) & 255) == 0) return;
    dl->AddRectFilled(Min(r), Max(r), col, Dv(rounding));
}
void Stroke(ImDrawList* dl, const R& r, ImU32 col, float rounding, float thickness) {
    dl->AddRect(Min(r), Max(r), col, Dv(rounding), 0, Dv(thickness));
}
void Circle(ImDrawList* dl, float cx, float cy, float r, ImU32 col) { dl->AddCircleFilled(ImVec2(Dv(cx), Dv(cy)), Dv(r), col); }
void Line(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 col, float thickness) {
    dl->AddLine(ImVec2(Dv(x0), Dv(y0)), ImVec2(Dv(x1), Dv(y1)), col, Dv(thickness));
}
void PushClip(const R& r) { ImGui::PushClipRect(Min(r), Max(r), true); }
void PopClip() { ImGui::PopClipRect(); }

// Text.
static unsigned Decode(const char*& s) {
    unsigned c = 0;
    const int n = ImTextCharFromUtf8(&c, s, nullptr);
    s += n > 0 ? n : 1;
    return c;
}

float TextW(ImFont* f, const char* s, float spacing) {
    float w = 0.f;
    while (*s) {
        const unsigned c = Decode(s);
        if (const ImFontGlyph* g = f->FindGlyph((ImWchar)c)) w += g->AdvanceX * g_fk;
        w += Dv(spacing);
    }
    return w / g_s;
}

// Baseline-anchored, with sub-pixel x and the baseline on a whole pixel.
void TextAt(ImDrawList* dl, ImFont* f, float x, float baseline, ImU32 col, const char* s, float spacing) {
    float px = Dv(x);
    const float py = Snap(baseline) - f->Ascent * g_fk;
    dl->PushTextureID(f->ContainerAtlas->TexID);
    while (*s) {
        const unsigned c = Decode(s);
        const ImFontGlyph* g = f->FindGlyph((ImWchar)c);
        if (!g) continue;
        if (g->Visible) {
            dl->PrimReserve(6, 4);
            dl->PrimRectUV(ImVec2(px + g->X0 * g_fk, py + g->Y0 * g_fk), ImVec2(px + g->X1 * g_fk, py + g->Y1 * g_fk),
                           ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V1), col);
        }
        px += g->AdvanceX * g_fk + Dv(spacing);
    }
    dl->PopTextureID();
}
void TextRight(ImDrawList* dl, ImFont* f, float x1, float baseline, ImU32 col, const char* s, float spacing) {
    TextAt(dl, f, x1 - TextW(f, s, spacing), baseline, col, s, spacing);
}
void TextCenter(ImDrawList* dl, ImFont* f, float cx, float baseline, ImU32 col, const char* s, float spacing) {
    TextAt(dl, f, cx - TextW(f, s, spacing) * 0.5f, baseline, col, s, spacing);
}

std::string Ellipsize(ImFont* f, const std::string& s, float maxW) {
    if (TextW(f, s.c_str()) <= maxW) return s;
    const char* ell = "\xE2\x80\xA6";
    const float ellW = TextW(f, ell);
    std::string out;
    float w = 0.f;
    for (const char* p = s.c_str(); *p;) {
        const char* start = p;
        const unsigned c = Decode(p);
        const ImFontGlyph* g = f->FindGlyph((ImWchar)c);
        const float adv = g ? g->AdvanceX * g_fk / g_s : 0.f;
        if (w + adv + ellW > maxW) break;
        w += adv;
        out.append(start, p);
    }
    return out + ell;
}

// Shadows and motion.
// A soft shadow as stacked rounded rects whose opacities follow a Gaussian edge.
static float NormCdf(float x) { return 0.5f * erfcf(-x * 0.70710678f); }
void ShadowRect(ImDrawList* dl, const R& r, float rounding, float offY, float blur, float alpha) {
    const int N = 32;
    const float sigma = blur * 0.5f;
    float prev = 0.f;
    for (int i = 0; i < N; ++i) {
        const float e = blur - 2.f * blur * (float)i / (float)(N - 1);
        const float O = alpha * NormCdf(-e / sigma);
        const float a = 1.f - (1.f - O) / (1.f - prev);
        prev = O;
        if (a <= 0.0005f) continue;
        const R rr{r.x - e, r.y + offY - e, r.w + 2.f * e, r.h + 2.f * e};
        if (rr.w <= 0.f || rr.h <= 0.f) continue;
        dl->AddRectFilled(ImVec2(Dv(rr.x), Dv(rr.y)), ImVec2(Dv(rr.x1()), Dv(rr.y1())), Black(a), Dv(std::max(0.f, rounding + e)));
    }
}
static void ShadowCircle(ImDrawList* dl, float cx, float cy, float rad, float offY, float blur, float alpha) {
    const int N = 10;
    const float sigma = blur * 0.5f;
    float prev = 0.f;
    for (int i = 0; i < N; ++i) {
        const float e = blur - 2.f * blur * (float)i / (float)(N - 1);
        const float O = alpha * NormCdf(-e / sigma);
        const float a = 1.f - (1.f - O) / (1.f - prev);
        prev = O;
        if (a <= 0.0005f || rad + e <= 0.f) continue;
        dl->AddCircleFilled(ImVec2(Dv(cx), Dv(cy + offY)), Dv(rad + e), Black(a));
    }
}

// cubic-bezier(.25, .1, .25, 1)
float EaseCss(float x) {
    x = std::clamp(x, 0.f, 1.f);
    const float x1 = .25f, y1 = .1f, x2 = .25f, y2 = 1.f;
    float t = x;
    for (int i = 0; i < 8; ++i) {
        const float xt = 3.f * (1 - t) * (1 - t) * t * x1 + 3.f * (1 - t) * t * t * x2 + t * t * t;
        const float dx = 3.f * (1 - t) * (1 - t) * x1 + 6.f * (1 - t) * t * (x2 - x1) + 3.f * t * t * (1.f - x2);
        if (fabsf(dx) < 1e-6f) break;
        t = std::clamp(t - (xt - x) / dx, 0.f, 1.f);
    }
    return 3.f * (1 - t) * (1 - t) * t * y1 + 3.f * (1 - t) * t * t * y2 + t * t * t;
}

// A transition on one number: when the target changes it eases there over `dur`. The first time an
// id is seen it starts at its target.
namespace {
struct Anim { bool init = false; float value = 0, from = 0, to = 0; double t0 = 0; float dur = 0; };
std::unordered_map<ImGuiID, Anim> g_anims;
}  // namespace
float Animate(ImGuiID id, float target, float dur) {
    Anim& a = g_anims[id];
    if (!a.init) {
        a = Anim{true, target, target, target, g_now, dur};
        return target;
    }
    if (target != a.to) {
        a.from = a.value;
        a.to = target;
        a.t0 = g_now;
        a.dur = dur;
    }
    if (a.dur <= 0.f) return a.value = a.to;
    const float t = (float)((g_now - a.t0) / a.dur);
    a.value = t >= 1.f ? a.to : a.from + (a.to - a.from) * EaseCss(t);
    return a.value;
}

float SmoothTo(float current, float target, float dt) {
    if (fabsf(target - current) < 0.05f) return target;
    return current + (target - current) * (1.f - expf(-dt * 14.f));
}

// Input.
// Every clickable rectangle is an invisible button. ImGui gives the hover to the first item under
// the pointer, so a control inside a clickable area is submitted before its container.
Hit Item(const char* id, const R& r) {
    Hit h;
    if (!g_interactive) return h;
    const ImVec2 a = Min(r), b = Max(r);
    ImGui::SetCursorScreenPos(a);
    h.clicked = ImGui::InvisibleButton(id, ImVec2(std::max(1.f, b.x - a.x), std::max(1.f, b.y - a.y)));
    h.hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    h.active = ImGui::IsItemActive();
    h.activated = ImGui::IsItemActivated();
    h.id = ImGui::GetItemID();
    return h;
}
void Pointer(const Hit& h) {
    if (h.hovered) g_cursor = ImGuiMouseCursor_Hand;
}

namespace {
struct { ImGuiID id = 0; float x0 = 0.f, w = 1.f; } g_drag;
float DragFraction(const Hit& h, const R& strip) {
    if (h.activated || g_drag.id != h.id) {
        g_drag.id = h.id;
        g_drag.x0 = Dv(strip.x);
        g_drag.w = std::max(1.f, Dv(strip.w));
    }
    return std::clamp((ImGui::GetIO().MousePos.x - g_drag.x0) / g_drag.w, 0.f, 1.f);
}

struct Editor {
    EditKind kind = EditKind::None;
    ImGuiID target = 0;
    std::string text;
    int caret = 0;
    bool selectAll = false;
    R field;
    bool fieldSeen = false, openedThisFrame = false;
} g_edit;
struct { EditKind kind = EditKind::None; ImGuiID target = 0; std::string text; bool pending = false; } g_commit;

bool CtrlOnly(const ImGuiIO& io) { return io.KeyCtrl && !io.KeyAlt; }
size_t MaxLen(EditKind k) { return k == EditKind::Value ? 16 : k == EditKind::Rename ? 40 : 256; }
bool Allowed(EditKind k, unsigned ch) {
    if (k == EditKind::Value) return (ch >= '0' && ch <= '9') || ch == '.' || ch == '-';
    if (k == EditKind::Rename) return ch >= 32 && ch != 127 && !std::strchr("\\/:*?\"<>|", (int)ch);
    return ch >= 32 && ch != 127;
}
std::string Clipboard(EditKind k) {
    std::string out;
    const char* clip = ImGui::GetClipboardText();
    for (const char* p = clip ? clip : ""; *p && out.size() < MaxLen(k);) {
        const unsigned ch = Decode(p);
        if (!Allowed(k, ch)) continue;
        char buf[5];
        ImTextCharToUtf8(buf, ch);
        out += buf;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '"')) out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '"')) out.erase(0, 1);
    return out;
}
void Commit() {
    g_commit = {g_edit.kind, g_edit.target, g_edit.text, true};
    EndEdit();
}
}  // namespace

void BeginEdit(EditKind kind, ImGuiID target, const std::string& text) {
    g_edit.kind = kind;
    g_edit.target = target;
    g_edit.text = text;
    g_edit.caret = (int)text.size();
    g_edit.selectAll = true;
    g_edit.openedThisFrame = true;
}
void EndEdit() {
    g_edit.kind = EditKind::None;
    g_edit.target = 0;
}
bool Editing() { return g_edit.kind != EditKind::None; }
bool EditingTarget(EditKind kind, ImGuiID target) { return g_edit.kind == kind && g_edit.target == target; }

bool TakeCommit(EditKind kind, ImGuiID target, std::string& text) {
    if (!g_commit.pending || g_commit.kind != kind || g_commit.target != target) return false;
    text = g_commit.text;
    g_commit.pending = false;
    return true;
}

void ProcessEditor(ImGuiIO& io) {
    if (g_edit.kind == EditKind::None) return;
    std::string& t = g_edit.text;
    int& c = g_edit.caret;
    bool& sel = g_edit.selectAll;
    c = std::clamp(c, 0, (int)t.size());
    auto clearSel = [&] {
        if (sel) {
            t.clear();
            c = 0;
            sel = false;
        }
    };
    auto insert = [&](unsigned ch) {
        if (!Allowed(g_edit.kind, ch) || t.size() >= MaxLen(g_edit.kind)) return;
        clearSel();
        char buf[5];
        ImTextCharToUtf8(buf, ch);
        t.insert((size_t)c, buf);
        c += (int)std::strlen(buf);
    };
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) insert((unsigned)io.InputQueueCharacters[i]);
    const bool inField = g_edit.fieldSeen && g_edit.field.contains(io.MousePos.x / g_s, io.MousePos.y / g_s);
    if (CtrlOnly(io) && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        sel = !t.empty();
        c = (int)t.size();
    }
    if (CtrlOnly(io) && (ImGui::IsKeyPressed(ImGuiKey_C, false) || ImGui::IsKeyPressed(ImGuiKey_X, false)) && !t.empty()) {
        ImGui::SetClipboardText(t.c_str());
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            t.clear();
            c = 0;
            sel = false;
        }
    }
    if ((CtrlOnly(io) && ImGui::IsKeyPressed(ImGuiKey_V, false)) || (ImGui::IsMouseClicked(1) && inField)) {
        const std::string clip = Clipboard(g_edit.kind);
        for (const char* p = clip.c_str(); *p;) insert(Decode(p));
    }
    auto prev = [&](int pos) {
        int p = pos;
        do --p;
        while (p > 0 && ((unsigned char)t[(size_t)p] & 0xC0) == 0x80);
        return p;
    };
    auto next = [&](int pos) {
        int p = pos + 1;
        while (p < (int)t.size() && ((unsigned char)t[(size_t)p] & 0xC0) == 0x80) ++p;
        return p;
    };
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
        if (sel) clearSel();
        else if (c > 0) {
            const int p = prev(c);
            t.erase((size_t)p, (size_t)(c - p));
            c = p;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, true)) {
        if (sel) clearSel();
        else if (c < (int)t.size()) t.erase((size_t)c, (size_t)(next(c) - c));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
        sel = false;
        if (c > 0) c = prev(c);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
        sel = false;
        if (c < (int)t.size()) c = next(c);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) sel = false, c = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) sel = false, c = (int)t.size();
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) Commit();
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) EndEdit();
    else if (ImGui::IsMouseClicked(0)) {
        if (inField) sel = false;
        else Commit();
    }
}

void EditFieldHit(const R& field) {
    (void)Item("edit:field", field);
    g_edit.field = field;
    g_edit.fieldSeen = true;
}

void DrawEditField(ImDrawList* dl, const R& field, ImFont* f, float pad) {
    Fill(dl, field, White(0.06f), field.h > 26.f ? 6.f : 5.f);
    const std::string before = g_edit.text.substr(0, (size_t)std::clamp(g_edit.caret, 0, (int)g_edit.text.size()));
    const float caretW = TextW(f, before.c_str());
    const float sx = std::max(0.f, caretW - (field.w - 2.f * pad));
    PushClip(R{field.x + 4.f, field.y, field.w - 8.f, field.h});
    if (g_edit.selectAll && !g_edit.text.empty())
        Fill(dl, R{field.x + pad - sx, field.y + (field.h - 16.f) * 0.5f, TextW(f, g_edit.text.c_str()), 16.f}, Accent(0.35f), 2.f);
    TextAt(dl, f, field.x + pad - sx, field.y + (field.h - 18.f) * 0.5f + LM(13.f).asc, Hex(kText), g_edit.text.c_str());
    if (std::fmod(g_now, 1.0) < 0.55)
        Fill(dl, R{field.x + pad - sx + caretW, field.y + (field.h - 16.f) * 0.5f, 1.5f, 16.f}, Accent());
    PopClip();
}

void EndFrameEditor() {
    g_commit.pending = false;
    if (g_edit.kind != EditKind::None && !g_edit.fieldSeen && !g_edit.openedThisFrame) EndEdit();
    g_edit.fieldSeen = false;
    g_edit.openedThisFrame = false;
}

// Widgets.
void Knob(ImDrawList* dl, float cx, float cy, float rad, float blur, float alpha, bool rim) {
    ShadowCircle(dl, cx, cy, rad, 1.f, blur, alpha);
    Circle(dl, cx, cy, rad, White(1.f));
    if (rim) dl->AddCircle(ImVec2(Dv(cx), Dv(cy)), Dv(rad - 0.75f), Hex(kPanelBg), 0, Dv(1.5f));
}

// 34 x 18 pill; the knob slides over .15 s.
void DrawSwitch(ImDrawList* dl, const R& r, bool on, const char* animKey) {
    const float k = Animate(ImHashStr(animKey), on ? 1.f : 0.f, 0.15f);
    Fill(dl, r, Mix(White(0.1f), Accent(), k), 9.f);
    Knob(dl, r.x + 9.f + 16.f * k, r.y + 9.f, 7.f, 3.f, 0.3f, g_lightAccent && k > 0.5f);
}

bool SwitchRow(ImDrawList* dl, const char* id, const R& r, const char* label, bool& on) {
    TextAt(dl, F(400, 13.f), r.x, r.y + LM(13.f).asc, Hex(kLabel), label);
    const R sw{r.x1() - 34.f, r.y, 34.f, 18.f};
    const Hit h = Item(id, sw);
    Pointer(h);
    if (h.clicked) on = !on;
    DrawSwitch(dl, sw, on, id);
    return h.clicked;
}

float BindButtonWidth(const std::string& label) { return std::max(34.f, 16.f + TextW(F(600, 10.5f), label.c_str(), 0.63f)); }

Hit BindButton(ImDrawList* dl, const char* id, const R& r, const std::string& label, bool capturing) {
    const Hit h = Item(id, r);
    Pointer(h);
    Fill(dl, r, White(h.hovered ? 0.1f : 0.05f), 5.f);
    const char* text = capturing ? "..." : label.c_str();
    const ImU32 col = capturing ? Accent() : Hex(h.hovered ? kText : kBindText);
    TextAt(dl, F(600, 10.5f), r.x + (r.w - TextW(F(600, 10.5f), text, 0.63f)) * 0.5f, r.y + 5.f + LM(10.5f).asc, col, text, 0.63f);
    return h;
}

Hit TextButton(ImDrawList* dl, const char* id, const R& r, const char* label, bool primary) {
    const Hit h = Item(id, r);
    Pointer(h);
    if (primary) {
        const float k = h.hovered ? 1.1f : 1.f;
        Fill(dl, r, Brighten(Accent(), k), 6.f);
        TextCenter(dl, F(600, 12.f), r.x + r.w * 0.5f, r.y + 6.f + LM(12.f).asc, Brighten(OnAccent(), k), label);
    } else {
        Fill(dl, r, White(h.hovered ? 0.1f : 0.05f), 6.f);
        TextCenter(dl, F(600, 12.f), r.x + r.w * 0.5f, r.y + 6.f + LM(12.f).asc, Hex(h.hovered ? kText : kLabel), label);
    }
    return h;
}
float TextButtonWidth(const char* label) { return 24.f + TextW(F(600, 12.f), label); }

namespace {
int Decimals(float step) {
    if (step >= 1.f) return 0;
    if (step >= 0.1f) return 1;
    if (step >= 0.01f) return 2;
    return 3;
}
float SnapValue(float v, float mn, float mx, float step) {
    const float s = step > 0.f ? step : 1.f;
    v = RoundHalfUp(v / s) * s;
    const float k = powf(10.f, (float)Decimals(step));
    v = RoundHalfUp(v * k) / k;
    return std::clamp(v, mn, mx);
}
// The number beside a slider; a click turns it into the editor.
bool ValueText(ImDrawList* dl, const char* id, float x1, float rowY, const std::string& display, const std::string& number,
               float& typed) {
    const ImGuiID target = ImHashStr(id);
    ImFont* f = F(600, 13.f);
    std::string text;
    if (TakeCommit(EditKind::Value, target, text)) {
        char* end = nullptr;
        typed = std::strtof(text.c_str(), &end);
        return end && end != text.c_str();
    }
    if (EditingTarget(EditKind::Value, target)) {
        const R field{x1 - 76.f, rowY - 1.f, 76.f, 20.f};
        EditFieldHit(field);
        DrawEditField(dl, field, f, 6.f);
        return false;
    }
    const float w = TextW(f, display.c_str());
    const Hit h = Item(id, R{x1 - w - 4.f, rowY - 1.f, w + 8.f, 20.f});
    if (h.hovered) g_cursor = ImGuiMouseCursor_TextInput;
    if (h.clicked) BeginEdit(EditKind::Value, target, number);
    TextRight(dl, f, x1, rowY + LM(13.f).asc, Hex(kText), display.c_str());
    return false;
}
void Track(ImDrawList* dl, const R& r, float f0, float f1) {
    const R track{r.x, r.y + 32.f, r.w, 4.f};
    Fill(dl, track, White(0.08f), 2.f);
    if (f1 > f0) Fill(dl, R{track.x + track.w * f0, track.y, track.w * (f1 - f0), 4.f}, Accent(), 2.f);
}
}  // namespace

std::string NumberText(float v, float step) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", Decimals(step), v);
    return buf;
}

// 42 px: label and number over a 16 px strip holding the 4 px track.
void SliderUI(ImDrawList* dl, const char* id, const R& r, const char* label, float& v, float mn, float mx, float step,
              const char* unit) {
    const R strip{r.x, r.y + 26.f, r.w, 16.f};
    const Hit h = Item(id, strip);
    if (h.hovered || h.active) g_cursor = ImGuiMouseCursor_ResizeEW;
    if (h.active) v = SnapValue(mn + DragFraction(h, strip) * (mx - mn), mn, mx, step);
    TextAt(dl, F(400, 13.f), r.x, r.y + LM(13.f).asc, Hex(kLabel), label);
    char vid[96];
    std::snprintf(vid, sizeof vid, "%s:v", id);
    float typed = 0.f;
    if (ValueText(dl, vid, r.x1(), r.y, NumberText(v, step) + unit, NumberText(v, step), typed)) v = SnapValue(typed, mn, mx, step);
    const float frac = std::clamp(mx > mn ? (v - mn) / (mx - mn) : 0.f, 0.f, 1.f);
    Track(dl, r, 0.f, frac);
    Knob(dl, r.x + r.w * frac, r.y + 34.f, 6.f, 4.f, 0.4f, g_lightAccent);
}

// Two knobs on one track; a press takes the nearer one.
void RangeUI(ImDrawList* dl, const char* id, const R& r, const char* label, float& lo, float& hi, float mn, float mx,
             float step, const char* unit) {
    const R strip{r.x, r.y + 26.f, r.w, 16.f};
    const Hit h = Item(id, strip);
    if (h.hovered || h.active) g_cursor = ImGuiMouseCursor_ResizeEW;
    static int knob = 0;
    if (h.active && mx > mn) {
        const float f = DragFraction(h, strip);
        if (h.activated) knob = fabsf(f - (lo - mn) / (mx - mn)) <= fabsf(f - (hi - mn) / (mx - mn)) ? 0 : 1;
        if (lo == hi && h.activated) knob = f > (lo - mn) / (mx - mn) ? 1 : 0;
        const float nv = SnapValue(mn + f * (mx - mn), mn, mx, step);
        if (knob == 0) lo = std::min(nv, hi);
        else hi = std::max(nv, lo);
    }
    TextAt(dl, F(400, 13.f), r.x, r.y + LM(13.f).asc, Hex(kLabel), label);
    ImFont* f = F(600, 13.f);
    const std::string hiText = NumberText(hi, step) + unit, loText = NumberText(lo, step) + unit;
    const char* sep = " - ";
    char vid[96];
    float typed = 0.f;
    std::snprintf(vid, sizeof vid, "%s:v1", id);
    if (ValueText(dl, vid, r.x1(), r.y, hiText, NumberText(hi, step), typed)) hi = std::max(SnapValue(typed, mn, mx, step), lo);
    const float sepX1 = r.x1() - TextW(f, hiText.c_str());
    TextRight(dl, f, sepX1, r.y + LM(13.f).asc, Hex(kMuted), sep);
    std::snprintf(vid, sizeof vid, "%s:v0", id);
    if (ValueText(dl, vid, sepX1 - TextW(f, sep), r.y, loText, NumberText(lo, step), typed))
        lo = std::min(SnapValue(typed, mn, mx, step), hi);
    const float flo = mx > mn ? std::clamp((lo - mn) / (mx - mn), 0.f, 1.f) : 0.f;
    const float fhi = mx > mn ? std::clamp((hi - mn) / (mx - mn), 0.f, 1.f) : 0.f;
    Track(dl, r, flo, fhi);
    Knob(dl, r.x + r.w * flo, r.y + 34.f, 6.f, 4.f, 0.4f, g_lightAccent);
    Knob(dl, r.x + r.w * fhi, r.y + 34.f, 6.f, 4.f, 0.4f, g_lightAccent);
}

float SegmentedWidth(const std::vector<std::string>& options) {
    float total = 4.f;
    for (const auto& o : options) total += 20.f + TextW(F(600, 11.5f), o.c_str());
    return total + 2.f * (float)(options.empty() ? 0 : options.size() - 1);
}

// Right-aligned at x1, 27 px tall: one button per option in a 2 px well.
void SegmentedUI(ImDrawList* dl, const char* prefix, float x1, float y, const std::vector<std::string>& options, int& selected) {
    ImFont* f = F(600, 11.5f);
    const R box{x1 - SegmentedWidth(options), y, SegmentedWidth(options), 27.f};
    Fill(dl, box, White(0.05f), 6.f);
    float x = box.x + 2.f;
    for (size_t i = 0; i < options.size(); ++i) {
        const float w = 20.f + TextW(f, options[i].c_str());
        const R b{x, y + 2.f, w, 23.f};
        char id[96];
        std::snprintf(id, sizeof id, "%s:o%d", prefix, (int)i);
        const Hit h = Item(id, b);
        Pointer(h);
        if (h.clicked) selected = (int)i;
        const bool act = selected == (int)i;
        if (act) Fill(dl, b, White(0.1f), 5.f);
        TextAt(dl, f, b.x + 10.f, y + 6.f + LM(11.5f).asc, Hex(act || h.hovered ? kText : kMuted), options[i].c_str());
        x += w + 2.f;
    }
}

// For a long list: the current option between two arrows.
void StepperUI(ImDrawList* dl, const char* prefix, float x1, float y, const std::vector<std::string>& options, int& selected) {
    if (options.empty()) return;
    ImFont* f = F(600, 11.5f);
    float widest = 0.f;
    for (const auto& o : options) widest = std::max(widest, TextW(f, o.c_str()));
    const float w = 27.f + widest + 24.f + 27.f;
    const R box{x1 - w, y, w, 27.f};
    Fill(dl, box, White(0.05f), 6.f);
    const int n = (int)options.size();
    selected = std::clamp(selected, 0, n - 1);
    char id[96];
    std::snprintf(id, sizeof id, "%s:prev", prefix);
    const Hit hp = Item(id, R{box.x, y, 27.f, 27.f});
    Pointer(hp);
    if (hp.clicked) selected = (selected + n - 1) % n;
    std::snprintf(id, sizeof id, "%s:next", prefix);
    const Hit hn = Item(id, R{box.x1() - 27.f, y, 27.f, 27.f});
    Pointer(hn);
    if (hn.clicked) selected = (selected + 1) % n;
    DrawChevron(dl, box.x + 13.5f, y + 13.5f, 3.5f, false, Hex(hp.hovered ? kText : kMuted));
    DrawChevron(dl, box.x1() - 13.5f, y + 13.5f, 3.5f, true, Hex(hn.hovered ? kText : kMuted));
    TextCenter(dl, f, box.x + w * 0.5f, y + 6.f + LM(11.5f).asc, Hex(kText), options[(size_t)selected].c_str());
}

bool TextField(ImDrawList* dl, const char* id, const R& field, const std::string& text, const char* placeholder,
               std::string& out) {
    const ImGuiID target = ImHashStr(id);
    ImFont* f = F(400, 13.f);
    if (TakeCommit(EditKind::Text, target, out)) return true;
    if (EditingTarget(EditKind::Text, target)) {
        EditFieldHit(field);
        DrawEditField(dl, field, f, 10.f);
        return false;
    }
    const Hit h = Item(id, field);
    if (h.hovered) g_cursor = ImGuiMouseCursor_TextInput;
    if (h.clicked) BeginEdit(EditKind::Text, target, text);
    Fill(dl, field, White(h.hovered ? 0.07f : 0.05f), 6.f);
    const std::string shown = text.empty() ? std::string(placeholder) : Ellipsize(f, text, field.w - 20.f);
    TextAt(dl, f, field.x + 10.f, field.y + (field.h - 18.f) * 0.5f + LM(13.f).asc, Hex(text.empty() ? kMuted : kText), shown.c_str());
    return false;
}

void DrawCross(ImDrawList* dl, float cx, float cy, float half, ImU32 col, float thickness) {
    Line(dl, cx - half, cy - half, cx + half, cy + half, col, thickness);
    Line(dl, cx + half, cy - half, cx - half, cy + half, col, thickness);
}

void DrawChevron(ImDrawList* dl, float cx, float cy, float half, bool right, ImU32 col) {
    const float d = right ? -half * 0.5f : half * 0.5f;
    const ImVec2 pts[3] = {ImVec2(Dv(cx + d), Dv(cy - half)), ImVec2(Dv(cx - d), Dv(cy)), ImVec2(Dv(cx + d), Dv(cy + half))};
    dl->AddPolyline(pts, 3, col, 0, Dv(1.5f));
}

// Lucide's pencil, flattened to a polyline.
void DrawPencil(ImDrawList* dl, float cx, float cy, ImU32 col) {
    static const ImVec2 kOutline[] = {
        {21.174f, 6.812f}, {21.785f, 5.898f}, {22.000f, 4.819f}, {21.785f, 3.740f}, {21.174f, 2.826f}, {20.260f, 2.214f},
        {19.181f, 2.000f}, {18.103f, 2.214f}, {17.188f, 2.825f}, {3.842f, 16.174f}, {3.631f, 16.422f}, {3.463f, 16.701f},
        {3.342f, 17.004f}, {2.021f, 21.356f}, {2.015f, 21.622f}, {2.147f, 21.853f}, {2.378f, 21.984f}, {2.644f, 21.978f},
        {6.997f, 20.658f}, {7.300f, 20.538f}, {7.579f, 20.371f}, {7.827f, 20.161f}};
    const float u = Dv(16.f / 24.f);
    const ImVec2 o(Dv(cx) - 12.f * u, Dv(cy) - 12.f * u);
    constexpr int n = (int)(sizeof kOutline / sizeof kOutline[0]);
    ImVec2 pts[n];
    for (int i = 0; i < n; ++i) pts[i] = ImVec2(o.x + kOutline[i].x * u, o.y + kOutline[i].y * u);
    dl->AddPolyline(pts, n, col, ImDrawFlags_Closed, 2.f * u);
    for (int i = 0; i < n; ++i) dl->AddCircleFilled(pts[i], u, col);
    const ImVec2 a(o.x + 15.f * u, o.y + 5.f * u), b(o.x + 19.f * u, o.y + 9.f * u);
    dl->AddLine(a, b, col, 2.f * u);
    dl->AddCircleFilled(a, u, col);
    dl->AddCircleFilled(b, u, col);
}

void SelectionRing(ImDrawList* dl, float cx, float cy, float rad, unsigned bg) {
    Circle(dl, cx, cy, rad + 3.5f, Hex(kText));
    Circle(dl, cx, cy, rad + 2.f, Hex(bg));
}

void DrawHueWheel(ImDrawList* dl, float cx, float cy, float rad) {
    const int N = 48;
    const ImVec2 c(Dv(cx), Dv(cy));
    const float rIn = Dv(rad) - 0.5f, rOut = Dv(rad) + 0.5f;
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    dl->PrimReserve(N * 9, 1 + 2 * N);
    const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(c, uv, White(1.f));
    for (int i = 0; i < N; ++i) {
        const float a = 6.2831853f * (float)i / (float)N;
        const ImU32 col = Hex(HsvToRgb((float)i / (float)N, 1.f, 1.f));
        dl->PrimWriteVtx(ImVec2(c.x + cosf(a) * rIn, c.y + sinf(a) * rIn), uv, col);
        dl->PrimWriteVtx(ImVec2(c.x + cosf(a) * rOut, c.y + sinf(a) * rOut), uv, col & ~IM_COL32_A_MASK);
    }
    for (int i = 0; i < N; ++i) {
        const ImDrawIdx in0 = (ImDrawIdx)(base + 1 + 2 * i), out0 = (ImDrawIdx)(in0 + 1);
        const ImDrawIdx in1 = (ImDrawIdx)(base + 1 + 2 * ((i + 1) % N)), out1 = (ImDrawIdx)(in1 + 1);
        dl->PrimWriteIdx(base); dl->PrimWriteIdx(in0); dl->PrimWriteIdx(in1);
        dl->PrimWriteIdx(in0); dl->PrimWriteIdx(out0); dl->PrimWriteIdx(out1);
        dl->PrimWriteIdx(in0); dl->PrimWriteIdx(out1); dl->PrimWriteIdx(in1);
    }
}

void BeginFrame(float scale, float fontScale, unsigned accent) {
    g_s = scale;
    g_fk = fontScale > 0.f ? scale / fontScale : 1.f;
    g_now = ImGui::GetTime();
    g_accent = accent ? accent : kDefaultAccent;
    g_lightAccent = Luminance(g_accent) > 0.45f;
    g_cursor = ImGuiMouseCursor_Arrow;
    g_interactive = true;
    g_alpha = 1.f;
}

}  // namespace citron::ui
