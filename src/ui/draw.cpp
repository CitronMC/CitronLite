#include "draw.h"
#include "citron/icons_data.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>

namespace citron::ui {

const unsigned kSwatches[7] = {tok::kLime, 0x3DDC97, 0x5EA8FF, 0x9B7BFF, 0xFF7A6B, 0xF2B544, 0xECEEE6};

float g_s = 1.f;
double g_now = 0.0;

namespace {

float g_fk = 1.f;
float g_dt = 0.f;
unsigned long long g_frame = 0, g_frames[2] = {};
unsigned g_accent = tok::kLime;
bool g_lightAccent = true;
bool g_interactive = true;
ImGuiMouseCursor g_cursor = ImGuiMouseCursor_Arrow;
ImDrawList* g_dl = nullptr;
std::vector<float> g_alpha = {1.f};

float Luminance(unsigned rgb) {
    auto lin = [](float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(((rgb >> 16) & 255) / 255.f) + 0.7152f * lin(((rgb >> 8) & 255) / 255.f) +
           0.0722f * lin((rgb & 255) / 255.f);
}

}  // namespace

Col Hex(unsigned rgb, float a) {
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (int)(a * g_alpha.back() * 255.f + 0.5f));
}
Col White(float a) { return Hex(0xffffff, a); }
Col Black(float a) { return Hex(0x000000, a); }
Col AccentCol(float a) { return Hex(g_accent, a); }
Col OnAccentCol() { return Hex(g_lightAccent ? tok::kOnLime : tok::kText); }

// Colours mix in premultiplied space, the way a browser transitions them.
Col Mix(Col a, Col b, float t) {
    const float aa = ((a >> IM_COL32_A_SHIFT) & 255) / 255.f, ab = ((b >> IM_COL32_A_SHIFT) & 255) / 255.f;
    const float al = aa + (ab - aa) * t;
    if (al <= 0.0001f) return IM_COL32(0, 0, 0, 0);
    auto ch = [&](int sh) {
        const float ca = ((a >> sh) & 255) / 255.f, cb = ((b >> sh) & 255) / 255.f;
        return (ca * aa * (1.f - t) + cb * ab * t) / al;
    };
    return ImGui::ColorConvertFloat4ToU32(ImVec4(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), al));
}

void PushAlpha(float a) { g_alpha.push_back(g_alpha.back() * a); }
void PopAlpha() {
    if (g_alpha.size() > 1) g_alpha.pop_back();
}

// Geometry.
static float Dv(float v) { return v * g_s; }
static float Snap(float v) { return floorf(v * g_s + 0.5f); }
static ImVec2 P(float x, float y) { return ImVec2(Dv(x), Dv(y)); }
ImVec2 Min(const R& r) { return ImVec2(Snap(r.x), Snap(r.y)); }
ImVec2 Max(const R& r) { return ImVec2(Snap(r.x1()), Snap(r.y1())); }
ImVec2 Mouse() { return ImVec2(ImGui::GetIO().MousePos.x / g_s, ImGui::GetIO().MousePos.y / g_s); }
ImDrawList* DL() { return g_dl; }

void Fill(const R& r, Col c, float radius) {
    if (((c >> IM_COL32_A_SHIFT) & 255) == 0) return;
    g_dl->AddRectFilled(Min(r), Max(r), c, Dv(radius));
}
void Stroke(const R& r, Col c, float radius, float thickness) {
    const float t = Dv(thickness), half = t * 0.5f;
    const ImVec2 a = Min(r), b = Max(r);
    g_dl->AddRect(ImVec2(a.x + half, a.y + half), ImVec2(b.x - half, b.y - half), c, std::max(0.f, Dv(radius) - half), 0, t);
}
void Circle(float cx, float cy, float r, Col c) { g_dl->AddCircleFilled(P(cx, cy), Dv(r), c); }
void Ring(float cx, float cy, float r, Col c, float thickness) { g_dl->AddCircle(P(cx, cy), Dv(r), c, 0, Dv(thickness)); }
void Line(float x0, float y0, float x1, float y1, Col c, float thickness) { g_dl->AddLine(P(x0, y0), P(x1, y1), c, Dv(thickness)); }
void PushClip(const R& r) { ImGui::PushClipRect(Min(r), Max(r), true); }
void PopClip() { ImGui::PopClipRect(); }

// Soft shadows: stacked layers whose opacities follow a Gaussian edge.
static float NormCdf(float x) { return 0.5f * erfcf(-x * 0.70710678f); }
void Shadow(const R& r, float radius, float offY, float blur, float alpha) {
    const int N = 24;
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
        g_dl->AddRectFilled(P(rr.x, rr.y), P(rr.x1(), rr.y1()), Black(a), Dv(std::max(0.f, radius + e)));
    }
}
static void ShadowCircle(float cx, float cy, float rad, float offY, float blur, float alpha) {
    const int N = 10;
    const float sigma = blur * 0.5f;
    float prev = 0.f;
    for (int i = 0; i < N; ++i) {
        const float e = blur - 2.f * blur * (float)i / (float)(N - 1);
        const float O = alpha * NormCdf(-e / sigma);
        const float a = 1.f - (1.f - O) / (1.f - prev);
        prev = O;
        if (a <= 0.0005f || rad + e <= 0.f) continue;
        g_dl->AddCircleFilled(P(cx, cy + offY), Dv(rad + e), Black(a));
    }
}

// Text. ImGui sizes a font by its ascender minus its descender, so an em of S takes S * emFactor;
// capRatio is the cap height per em.
namespace {
struct FaceInfo { float emFactor, capRatio; };
constexpr FaceInfo kFaces[(int)Face::Count] = {
    {1303.f / 1000.f, 697.f / 1000.f},  {1303.f / 1000.f, 697.f / 1000.f},  {1303.f / 1000.f, 697.f / 1000.f},
    {2528.f / 2048.f, 1440.f / 2048.f}, {2528.f / 2048.f, 1440.f / 2048.f}, {1320.f / 1000.f, 730.f / 1000.f},
};
struct Slot { Face face; float size; ImFont* font; };
Slot g_slots[] = {
    {Face::Regular, 11.f, nullptr},     {Face::Regular, 11.5f, nullptr},  {Face::Regular, 12.f, nullptr},
    {Face::Regular, 12.5f, nullptr},    {Face::Regular, 13.f, nullptr},   {Face::Medium, 12.f, nullptr},
    {Face::Medium, 12.5f, nullptr},     {Face::Medium, 13.f, nullptr},    {Face::SemiBold, 11.f, nullptr},
    {Face::SemiBold, 12.5f, nullptr},   {Face::Title, 15.f, nullptr},     {Face::TitleBold, 21.f, nullptr},
    {Face::MonoMedium, 10.f, nullptr},  {Face::MonoMedium, 10.5f, nullptr}, {Face::MonoMedium, 11.f, nullptr},
    {Face::MonoMedium, 12.f, nullptr},  {Face::MonoMedium, 12.5f, nullptr},
};

// The nearest slot of the face, and the factor to scale its glyphs by.
const Slot& Find(Face f, float size, float& k) {
    const Slot* best = nullptr;
    for (const Slot& s : g_slots)
        if (s.face == f && (!best || fabsf(s.size - size) < fabsf(best->size - size))) best = &s;
    k = g_fk * size / best->size;
    return *best;
}

unsigned Decode(const char*& s) {
    unsigned c = 0;
    const int n = ImTextCharFromUtf8(&c, s, nullptr);
    s += n > 0 ? n : 1;
    return c;
}
}  // namespace

void BuildFonts(ImFontAtlas* atlas, const FontFile (&files)[(int)Face::Count], float scale) {
    atlas->Clear();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2026, 0x2026, 0};
    for (Slot& s : g_slots) {
        const FontFile& ff = files[(int)s.face];
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        cfg.OversampleH = s.size * scale >= 18.f ? 2 : 3;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = false;
        s.font = atlas->AddFontFromMemoryTTF(const_cast<void*>(ff.data), ff.size, s.size * kFaces[(int)s.face].emFactor * scale, &cfg, ranges);
    }
}

float CapH(Face f, float size) { return size * kFaces[(int)f].capRatio; }

float TextW(Face f, float size, const char* s, float tracking) {
    float k;
    ImFont* font = Find(f, size, k).font;
    float w = 0.f;
    while (*s) {
        const unsigned c = Decode(s);
        if (const ImFontGlyph* g = font->FindGlyph((ImWchar)c)) w += g->AdvanceX * k;
        w += Dv(tracking);
    }
    return w / g_s;
}

// Sub-pixel x, the baseline on a whole pixel.
void Text(Face f, float size, float x, float baseline, Col c, const char* s, float tracking) {
    if (((c >> IM_COL32_A_SHIFT) & 255) == 0) return;
    float k;
    ImFont* font = Find(f, size, k).font;
    float px = Dv(x);
    const float py = Snap(baseline) - font->Ascent * k;
    g_dl->PushTextureID(font->ContainerAtlas->TexID);
    while (*s) {
        const unsigned ch = Decode(s);
        const ImFontGlyph* g = font->FindGlyph((ImWchar)ch);
        if (!g) continue;
        if (g->Visible) {
            g_dl->PrimReserve(6, 4);
            g_dl->PrimRectUV(ImVec2(px + g->X0 * k, py + g->Y0 * k), ImVec2(px + g->X1 * k, py + g->Y1 * k), ImVec2(g->U0, g->V0),
                             ImVec2(g->U1, g->V1), c);
        }
        px += g->AdvanceX * k + Dv(tracking);
    }
    g_dl->PopTextureID();
}
void TextMid(Face f, float size, float x, float cy, Col c, const char* s, float tracking) {
    Text(f, size, x, cy + CapH(f, size) * 0.5f, c, s, tracking);
}
void TextMidR(Face f, float size, float x1, float cy, Col c, const char* s, float tracking) {
    TextMid(f, size, x1 - TextW(f, size, s, tracking), cy, c, s, tracking);
}
void TextMidC(Face f, float size, float cx, float cy, Col c, const char* s, float tracking) {
    TextMid(f, size, cx - TextW(f, size, s, tracking) * 0.5f, cy, c, s, tracking);
}

std::string Ellipsize(Face f, float size, const std::string& s, float maxW) {
    if (TextW(f, size, s.c_str()) <= maxW) return s;
    const char* ell = "\xE2\x80\xA6";
    const float ellW = TextW(f, size, ell);
    float k;
    ImFont* font = Find(f, size, k).font;
    std::string out;
    float w = 0.f;
    for (const char* p = s.c_str(); *p;) {
        const char* start = p;
        const unsigned c = Decode(p);
        const ImFontGlyph* g = font->FindGlyph((ImWchar)c);
        const float adv = g ? g->AdvanceX * k / g_s : 0.f;
        if (w + adv + ellW > maxW) break;
        w += adv;
        out.append(start, p);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + ell;
}

// Motion.
float Dt() { return g_dt; }
ImGuiID Id(const char* s, int n) { return ImHashStr(s, 0, (ImGuiID)(0x9E3779B9u * (unsigned)(n + 1))); }

static float Bezier(float x, float x1, float y1, float x2, float y2) {
    float t = x;
    for (int i = 0; i < 8; ++i) {
        const float xt = 3.f * (1 - t) * (1 - t) * t * x1 + 3.f * (1 - t) * t * t * x2 + t * t * t;
        const float dx = 3.f * (1 - t) * (1 - t) * x1 + 6.f * (1 - t) * t * (x2 - x1) + 3.f * t * t * (1.f - x2);
        if (fabsf(dx) < 1e-6f) break;
        t = std::clamp(t - (xt - x) / dx, 0.f, 1.f);
    }
    return 3.f * (1 - t) * (1 - t) * t * y1 + 3.f * (1 - t) * t * t * y2 + t * t * t;
}
float EaseOutT(float t) { return Bezier(std::clamp(t, 0.f, 1.f), .2f, .7f, .3f, 1.f); }

namespace {
struct EaseAnim { float value = 0, from = 0, to = 0; double t0 = 0; float dur = 0; unsigned long long seen = 0; };
struct SpringAnim { float x = 0, v = 0; unsigned long long seen = 0; };
std::unordered_map<ImGuiID, EaseAnim> g_ease;
std::unordered_map<ImGuiID, SpringAnim> g_spring;
}  // namespace

// When the target changes it eases there over `dur`. New, or not drawn last frame, it starts at its target.
float Ease(ImGuiID id, float target, float dur) {
    auto it = g_ease.find(id);
    if (it == g_ease.end() || it->second.seen + 1 < g_frame) {
        g_ease[id] = EaseAnim{target, target, target, g_now, dur, g_frame};
        return target;
    }
    EaseAnim& a = it->second;
    a.seen = g_frame;
    if (target != a.to) {
        a.from = a.value;
        a.to = target;
        a.t0 = g_now;
        a.dur = dur;
    }
    if (a.dur <= 0.f) return a.value = a.to;
    const float t = (float)((g_now - a.t0) / a.dur);
    a.value = t >= 1.f ? a.to : a.from + (a.to - a.from) * EaseOutT(t);
    return a.value;
}

float Spring(ImGuiID id, float target, float omega, float zeta) {
    auto it = g_spring.find(id);
    if (it == g_spring.end() || it->second.seen + 1 < g_frame) {
        g_spring[id] = SpringAnim{target, 0.f, g_frame};
        return target;
    }
    SpringAnim& s = it->second;
    s.seen = g_frame;
    float remaining = std::min(g_dt, 0.5f);
    while (remaining > 0.f) {
        const float h = std::min(remaining, 0.002f);
        const float acc = -2.f * zeta * omega * s.v - omega * omega * (s.x - target);
        s.v += acc * h;
        s.x += s.v * h;
        remaining -= h;
    }
    if (fabsf(s.x - target) < 0.0005f && fabsf(s.v) < 0.005f) {
        s.x = target;
        s.v = 0.f;
    }
    return s.x;
}
void SnapSpring(ImGuiID id, float value) { g_spring[id] = SpringAnim{value, 0.f, g_frame}; }

float Approach(float current, float target, float rate) {
    if (fabsf(target - current) < 0.05f) return target;
    return current + (target - current) * (1.f - expf(-g_dt * rate));
}

// Input.
namespace {
bool g_record = false;
std::map<std::string, R> g_items, g_itemsBuilding;
}  // namespace

void RecordItems(bool on) { g_record = on; }
void Note(const char* id, const R& r) {
    if (g_record) g_itemsBuilding[id] = r;
}
bool ItemRect(const std::string& id, R& out) {
    auto it = g_items.find(id);
    if (it == g_items.end()) return false;
    out = it->second;
    return true;
}
std::vector<std::pair<std::string, R>> ItemRects() { return {g_items.begin(), g_items.end()}; }

Hit Item(const char* id, const R& r) {
    Hit h;
    Note(id, r);
    if (!g_interactive) return h;
    const ImVec2 a = Min(r), b = Max(r);
    ImGui::SetCursorScreenPos(a);
    h.clicked = ImGui::InvisibleButton(id, ImVec2(std::max(1.f, b.x - a.x), std::max(1.f, b.y - a.y)));
    h.hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    h.held = ImGui::IsItemActive();
    h.activated = ImGui::IsItemActivated();
    h.id = ImGui::GetItemID();
    return h;
}
void Cursor(ImGuiMouseCursor c) { g_cursor = c; }
void PointerOnHover(const Hit& h) {
    if (h.hovered) g_cursor = ImGuiMouseCursor_Hand;
}
void SetInteractive(bool on) { g_interactive = on; }
ImGuiMouseCursor FrameCursor() { return g_cursor; }

// A drag keeps the strip it started on, so the value keeps tracking the pointer whatever moves under it.
namespace {
struct { ImGuiID id = 0; R strip; } g_drag;
float DragFraction(const Hit& h, const R& strip) {
    if (h.activated || g_drag.id != h.id) {
        g_drag.id = h.id;
        g_drag.strip = strip;
    }
    return std::clamp((Mouse().x - g_drag.strip.x) / std::max(1.f, g_drag.strip.w), 0.f, 1.f);
}

// Where a knob is drawn: under the pointer while dragged, gliding to the value's own place otherwise.
float GlideFraction(ImGuiID id, float value, bool held, float drag) {
    struct Glide { float shown = -1.f; };
    static std::unordered_map<ImGuiID, Glide> glides;
    Glide& g = glides[id];
    value = std::clamp(value, 0.f, 1.f);
    if (held) return g.shown = std::clamp(drag, 0.f, 1.f);
    if (g.shown < 0.f) return g.shown = value;
    g.shown += (value - g.shown) * (1.f - expf(-std::min(g_dt, 0.1f) * 20.f));
    if (fabsf(value - g.shown) < 0.0005f) g.shown = value;
    return g.shown;
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
    if (k == EditKind::Value) return (ch >= '0' && ch <= '9') || ch == '.' || ch == ',' || ch == '-';
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
    const ImVec2 m = Mouse();
    const bool inField = g_edit.fieldSeen && g_edit.field.contains(m.x, m.y);
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

// The open field: a well ringed in the accent, the text scrolling to keep the caret in view.
void EditField(const R& field, Face f, float size, float pad) {
    (void)Item("edit:field", field);
    g_edit.field = field;
    g_edit.fieldSeen = true;
    Fill(field, Hex(tok::kWell), 7.f);
    Stroke(field, AccentCol(0.55f), 7.f, 1.f);
    const std::string before = g_edit.text.substr(0, (size_t)std::clamp(g_edit.caret, 0, (int)g_edit.text.size()));
    const float caretW = TextW(f, size, before.c_str());
    const float sx = std::max(0.f, caretW - (field.w - 2.f * pad));
    PushClip(R{field.x + 3.f, field.y, field.w - 6.f, field.h});
    if (g_edit.selectAll && !g_edit.text.empty())
        Fill(R{field.x + pad - sx, field.cy() - 8.f, TextW(f, size, g_edit.text.c_str()), 16.f}, AccentCol(0.3f), 2.f);
    TextMid(f, size, field.x + pad - sx, field.cy(), Hex(tok::kText), g_edit.text.c_str());
    if (std::fmod(g_now, 1.06) < 0.53) Fill(R{field.x + pad - sx + caretW, field.cy() - 7.f, 1.2f, 14.f}, AccentCol());
    PopClip();
    if (field.contains(Mouse().x, Mouse().y)) g_cursor = ImGuiMouseCursor_TextInput;
}

void EndFrameEditor() {
    g_commit.pending = false;
    if (g_edit.kind != EditKind::None && !g_edit.fieldSeen && !g_edit.openedThisFrame) EndEdit();
    g_edit.fieldSeen = false;
    g_edit.openedThisFrame = false;
}

// The switch: the accent's track and a dark knob when on, a grey knob on a quiet track when off. The
// knob is placed from the pill's own pixels, so it stays centred at any scale.
bool Switch(const char* id, const R& r, bool& on) {
    const Hit h = Item(id, r.Inset(-3.f));
    PointerOnHover(h);
    if (h.clicked) on = !on;
    const float k = Ease(Id(id, 1), on ? 1.f : 0.f, 0.16f);
    const float pos = Spring(Id(id, 2), on ? 1.f : 0.f, 30.f, 0.72f);
    const Col offTrack = White(h.hovered ? 0.15f : 0.10f);
    const Col onTrack = h.hovered ? Mix(AccentCol(), White(1.f), 0.08f) : AccentCol();
    Fill(r, Mix(offTrack, onTrack, k), r.h * 0.5f);
    const ImVec2 a = Min(r), b = Max(r);
    const float end = (b.y - a.y) * 0.5f, knob = end - Dv(2.f);
    g_dl->AddCircleFilled(ImVec2(a.x + end + (b.x - a.x - 2.f * end) * std::clamp(pos, -0.08f, 1.08f), a.y + end), knob,
                          Mix(Hex(0xd3d7ca), OnAccentCol(), k));
    return h.clicked;
}

// The checkbox; the whole label is clickable.
namespace {
constexpr float kBox = 15.f, kBoxGap = 9.f;
}
bool Checkbox(const char* id, float x, float cy, float maxW, const char* label, bool& on) {
    const float w = std::min(maxW, kBox + kBoxGap + TextW(Face::Regular, 13.f, label));
    const Hit h = Item(id, R{x - 3.f, cy - 13.f, w + 6.f, 26.f});
    PointerOnHover(h);
    if (h.clicked) on = !on;
    const float k = Ease(Id(id, 1), on ? 1.f : 0.f, 0.14f);
    const R box{x, cy - kBox * 0.5f, kBox, kBox};
    if (k < 1.f) Stroke(box, White(h.hovered ? 0.34f : 0.2f), 4.f, 1.5f);
    if (k > 0.f) {
        PushAlpha(k);
        Fill(box, AccentCol(), 4.f);
        DrawIcon(Icon::Check, box.cx(), box.cy() + 0.3f, 12.f, OnAccentCol());
        PopAlpha();
    }
    const std::string text = Ellipsize(Face::Regular, 13.f, label, w - kBox - kBoxGap + 0.5f);
    TextMid(Face::Regular, 13.f, x + kBox + kBoxGap, cy, Mix(Hex(tok::kLabel), Hex(tok::kText), std::max(k, h.hovered ? 0.6f : 0.f)), text.c_str());
    return h.clicked;
}

// Numbers.
namespace {
constexpr float kValueCol = 118.f, kTrackGap = 16.f;

float SnapValue(float v, float mn, float mx, float step) {
    const float s = step > 0.f ? step : 1.f;
    v = roundf(v / s) * s;
    v = roundf(v * 1000.f) / 1000.f;
    return std::clamp(v, mn, mx);
}
float TypedValue(float v, float mn, float mx) { return std::clamp(roundf(v * 1000.f) / 1000.f, mn, mx); }

// One decimal at least, three at most.
std::string SettingNumber(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", roundf(v * 1000.f) / 1000.f);
    std::string s = b;
    const size_t dot = s.find('.');
    if (dot != std::string::npos)
        while (s.size() > dot + 2 && s.back() == '0') s.pop_back();
    if (s == "-0.0") s = "0.0";
    return s;
}
bool ParseTyped(std::string t, float& out) {
    std::replace(t.begin(), t.end(), ',', '.');
    const char* p = t.c_str();
    while (*p == ' ') ++p;
    char* end = nullptr;
    const float v = strtof(p, &end);
    if (!end || end == p || !std::isfinite(v)) return false;
    out = v;
    return true;
}

// The number, or the field that replaces it. True with `typed` set on a commit.
bool ValueText(const char* id, float x1, float cy, const std::string& number, const char* unit, float& typed) {
    const ImGuiID target = Id(id);
    std::string text;
    if (TakeCommit(EditKind::Value, target, text)) return ParseTyped(text, typed);
    if (EditingTarget(EditKind::Value, target)) {
        const float fw = std::max(TextW(Face::MonoMedium, 12.5f, g_edit.text.c_str()) + 26.f, 58.f);
        EditField(R{x1 - fw, cy - 12.f, fw, 24.f}, Face::MonoMedium, 12.5f, 10.f);
        return false;
    }
    const float unitW = unit && *unit ? TextW(Face::Regular, 12.f, unit) : 0.f;
    const float numW = TextW(Face::MonoMedium, 12.5f, number.c_str());
    const Hit h = Item(id, R{x1 - unitW - numW - 4.f, cy - 11.f, unitW + numW + 8.f, 22.f});
    if (h.hovered) Cursor(ImGuiMouseCursor_TextInput);
    if (h.clicked) BeginEdit(EditKind::Value, target, number);
    if (unitW > 0.f) TextMidR(Face::Regular, 12.f, x1, cy, Hex(tok::kSub), unit);
    TextMidR(Face::MonoMedium, 12.5f, x1 - unitW, cy, Hex(h.hovered ? 0xffffff : tok::kText), number.c_str());
    return false;
}

void Thumb(const char* key, float cx, float cy, bool hot) {
    const float r = Spring(Id(key, 7), hot ? 7.f : 6.f, 34.f, 0.8f);
    ShadowCircle(cx, cy, r, 1.f, 4.f, 0.45f);
    Circle(cx, cy, r, Hex(tok::kCard));
    Ring(cx, cy, r - 1.f, AccentCol(), 2.f);
}

void Track(const R& track, float f0, float f1, bool hot) {
    Fill(track, White(0.08f), track.h * 0.5f);
    if (f1 > f0) Fill(R{track.x + track.w * f0, track.y, track.w * (f1 - f0), track.h}, AccentCol(hot ? 1.f : 0.92f), track.h * 0.5f);
}

void Label(const R& row, const char* label, float labelW, bool hot) {
    TextMid(Face::Regular, 13.f, row.x, row.cy(), Hex(hot ? tok::kText : tok::kLabel), Ellipsize(Face::Regular, 13.f, label, labelW - 8.f).c_str());
}
}  // namespace

bool Slider(const char* id, const R& row, const char* label, float labelW, float& v, float mn, float mx, float step, const char* unit) {
    const float before = v;
    char vid[128];
    std::snprintf(vid, sizeof vid, "%s#v", id);
    float typed = 0.f;
    if (ValueText(vid, row.x1(), row.cy(), SettingNumber(v), unit, typed)) v = TypedValue(typed, mn, mx);
    const R strip{row.x + labelW, row.y, row.w - labelW - kValueCol - kTrackGap, row.h};
    const Hit h = Item(id, strip);
    if (h.hovered || h.held) Cursor(ImGuiMouseCursor_ResizeEW);
    if (h.held && mx > mn) v = SnapValue(mn + DragFraction(h, strip) * (mx - mn), mn, mx, step);
    const bool hot = h.hovered || h.held;
    Label(row, label, labelW, hot);
    const float f = GlideFraction(Id(id, 8), mx > mn ? (v - mn) / (mx - mn) : 0.f, h.held && mx > mn, h.held ? DragFraction(h, strip) : 0.f);
    const R track{strip.x, row.cy() - 2.f, strip.w, 4.f};
    Track(track, 0.f, f, hot);
    Thumb(id, track.x + track.w * f, track.cy(), hot);
    return v != before;
}

// Two knobs on one track; a press takes the nearer one.
bool RangeSlider(const char* id, const R& row, const char* label, float labelW, float& lo, float& hi, float mn, float mx, float step, const char* unit) {
    const float lo0 = lo, hi0 = hi;
    char vid[128];
    float typed = 0.f;
    const std::string hiText = SettingNumber(hi), loText = SettingNumber(lo);
    std::snprintf(vid, sizeof vid, "%s#hi", id);
    float hiW = (unit && *unit ? TextW(Face::Regular, 12.f, unit) : 0.f) + TextW(Face::MonoMedium, 12.5f, hiText.c_str());
    if (EditingTarget(EditKind::Value, Id(vid))) hiW = std::max(TextW(Face::MonoMedium, 12.5f, g_edit.text.c_str()) + 26.f, 58.f);
    const float hiX = row.x1() - hiW;
    if (ValueText(vid, row.x1(), row.cy(), hiText, unit, typed)) hi = std::max(TypedValue(typed, mn, mx), lo);
    const char* dash = " - ";
    TextMidR(Face::MonoMedium, 12.5f, hiX, row.cy(), Hex(tok::kSub), dash);
    std::snprintf(vid, sizeof vid, "%s#lo", id);
    if (ValueText(vid, hiX - TextW(Face::MonoMedium, 12.5f, dash), row.cy(), loText, "", typed)) lo = std::min(TypedValue(typed, mn, mx), hi);

    const R strip{row.x + labelW, row.y, row.w - labelW - kValueCol - kTrackGap, row.h};
    const Hit h = Item(id, strip);
    if (h.hovered || h.held) Cursor(ImGuiMouseCursor_ResizeEW);
    static int knob = 0;
    if (h.held && mx > mn) {
        const float f = DragFraction(h, strip);
        if (h.activated) {
            const float flo = (lo - mn) / (mx - mn), fhi = (hi - mn) / (mx - mn);
            knob = fabsf(f - flo) < fabsf(f - fhi) ? 0 : (fabsf(f - flo) > fabsf(f - fhi) ? 1 : (f < flo ? 0 : 1));
        }
        const float nv = SnapValue(mn + f * (mx - mn), mn, mx, step);
        if (knob == 0) lo = std::min(nv, hi);
        else hi = std::max(nv, lo);
    }
    const bool hot = h.hovered || h.held;
    Label(row, label, labelW, hot);
    const float vlo = mx > mn ? std::clamp((lo - mn) / (mx - mn), 0.f, 1.f) : 0.f;
    const float vhi = mx > mn ? std::clamp((hi - mn) / (mx - mn), 0.f, 1.f) : 0.f;
    const float drag = h.held ? DragFraction(h, strip) : 0.f;
    const float flo = GlideFraction(Id(id, 8), vlo, h.held && knob == 0 && mx > mn, std::min(drag, vhi));
    const float fhi = GlideFraction(Id(id, 9), vhi, h.held && knob == 1 && mx > mn, std::max(drag, vlo));
    const R track{strip.x, row.cy() - 2.f, strip.w, 4.f};
    Track(track, flo, fhi, hot);
    char k0[128], k1[128];
    std::snprintf(k0, sizeof k0, "%s#k0", id);
    std::snprintf(k1, sizeof k1, "%s#k1", id);
    Thumb(k0, track.x + track.w * flo, track.cy(), hot && (!h.held || knob == 0));
    Thumb(k1, track.x + track.w * fhi, track.cy(), hot && (!h.held || knob == 1));
    return lo != lo0 || hi != hi0;
}

namespace {
void StackHead(const R& row, const char* label, const std::string& value, const char* unit, bool hot) {
    const float cy = row.y + 11.f;
    TextMid(Face::Regular, 12.5f, row.x, cy, Hex(hot ? tok::kText : tok::kLabel), label);
    const float unitW = unit && *unit ? TextW(Face::Regular, 11.5f, unit) : 0.f;
    if (unitW > 0.f) TextMidR(Face::Regular, 11.5f, row.x1(), cy, Hex(tok::kSub), unit);
    TextMidR(Face::MonoMedium, 11.f, row.x1() - unitW, cy, Hex(tok::kText), value.c_str());
}
}  // namespace

bool StackSlider(const char* id, const R& row, const char* label, float& v, float mn, float mx, float step, const char* unit) {
    const float before = v;
    const R strip{row.x, row.y + 19.f, row.w, 18.f};
    const R track{strip.x + 7.f, strip.cy() - 2.f, strip.w - 14.f, 4.f};
    const Hit h = Item(id, strip);
    if (h.hovered || h.held) Cursor(ImGuiMouseCursor_ResizeEW);
    if (h.held && mx > mn) v = SnapValue(mn + DragFraction(h, track) * (mx - mn), mn, mx, step);
    const bool hot = h.hovered || h.held;
    StackHead(row, label, SettingNumber(v), unit, hot);
    const float f = GlideFraction(Id(id, 8), mx > mn ? (v - mn) / (mx - mn) : 0.f, h.held && mx > mn, h.held ? DragFraction(h, track) : 0.f);
    Track(track, 0.f, f, hot);
    Thumb(id, track.x + track.w * f, track.cy(), hot);
    return v != before;
}

bool StackRange(const char* id, const R& row, const char* label, float& lo, float& hi, float mn, float mx, float step, const char* unit) {
    const float lo0 = lo, hi0 = hi;
    const R strip{row.x, row.y + 19.f, row.w, 18.f};
    const R track{strip.x + 7.f, strip.cy() - 2.f, strip.w - 14.f, 4.f};
    const Hit h = Item(id, strip);
    if (h.hovered || h.held) Cursor(ImGuiMouseCursor_ResizeEW);
    static int knob = 0;
    if (h.held && mx > mn) {
        const float f = DragFraction(h, track);
        if (h.activated) {
            const float flo = (lo - mn) / (mx - mn), fhi = (hi - mn) / (mx - mn);
            knob = fabsf(f - flo) < fabsf(f - fhi) ? 0 : (fabsf(f - flo) > fabsf(f - fhi) ? 1 : (f < flo ? 0 : 1));
        }
        const float nv = SnapValue(mn + f * (mx - mn), mn, mx, step);
        if (knob == 0) lo = std::min(nv, hi);
        else hi = std::max(nv, lo);
    }
    const bool hot = h.hovered || h.held;
    StackHead(row, label, SettingNumber(lo) + " - " + SettingNumber(hi), unit, hot);
    const float vlo = mx > mn ? std::clamp((lo - mn) / (mx - mn), 0.f, 1.f) : 0.f;
    const float vhi = mx > mn ? std::clamp((hi - mn) / (mx - mn), 0.f, 1.f) : 0.f;
    const float drag = h.held ? DragFraction(h, track) : 0.f;
    const float flo = GlideFraction(Id(id, 8), vlo, h.held && knob == 0 && mx > mn, std::min(drag, vhi));
    const float fhi = GlideFraction(Id(id, 9), vhi, h.held && knob == 1 && mx > mn, std::max(drag, vlo));
    Track(track, flo, fhi, hot);
    char k0[128], k1[128];
    std::snprintf(k0, sizeof k0, "%s#k0", id);
    std::snprintf(k1, sizeof k1, "%s#k1", id);
    Thumb(k0, track.x + track.w * flo, track.cy(), hot && (!h.held || knob == 0));
    Thumb(k1, track.x + track.w * fhi, track.cy(), hot && (!h.held || knob == 1));
    return lo != lo0 || hi != hi0;
}

// Choices.
namespace {
constexpr float kSegH = 26.f, kSegPadX = 11.f, kSegGap = 2.f;
}
float SegmentedWidth(const std::vector<std::string>& options) {
    float w = 4.f;
    for (const auto& o : options) w += 2.f * kSegPadX + TextW(Face::Medium, 12.f, o.c_str());
    if (options.size() > 1) w += kSegGap * (float)(options.size() - 1);
    return w;
}

// Right-aligned at x1, a sliding highlight behind the chosen option.
bool Segmented(const char* id, float x1, float cy, const std::vector<std::string>& options, int& sel, const bool* dots) {
    const float total = SegmentedWidth(options);
    const R box{x1 - total, cy - kSegH * 0.5f, total, kSegH};
    Fill(box, Hex(tok::kWell), 7.f);
    bool changed = false;
    std::vector<float> xs, ws;
    float x = box.x + 2.f;
    for (const auto& o : options) {
        const float w = 2.f * kSegPadX + TextW(Face::Medium, 12.f, o.c_str());
        xs.push_back(x);
        ws.push_back(w);
        x += w + kSegGap;
    }
    const int cur = std::clamp(sel, 0, (int)options.size() - 1);
    if (!options.empty()) {
        const float hx = Spring(Id(id, 11), xs[(size_t)cur] - box.x, 32.f, 0.86f);
        const float hw = Spring(Id(id, 12), ws[(size_t)cur], 32.f, 0.86f);
        Fill(R{box.x + hx, box.y + 2.f, hw, kSegH - 4.f}, AccentCol(0.14f), 5.5f);
    }
    for (size_t i = 0; i < options.size(); ++i) {
        char bid[128];
        std::snprintf(bid, sizeof bid, "%s#%d", id, (int)i);
        const R b{xs[i], box.y + 2.f, ws[i], kSegH - 4.f};
        const Hit h = Item(bid, b);
        PointerOnHover(h);
        if (h.clicked && (int)i != sel) {
            sel = (int)i;
            changed = true;
        }
        const float k = Ease(Id(bid, 4), (int)i == cur ? 1.f : 0.f, 0.14f);
        TextMidC(Face::Medium, 12.f, b.cx(), b.cy(), Mix(Hex(h.hovered ? tok::kText : tok::kMuted), AccentCol(), k), options[i].c_str());
        if (dots && dots[i]) Circle(b.x1() - 5.5f, b.cy(), 2.f, AccentCol());
    }
    return changed;
}

// The same choice as a value between two arrows, for one with too many options to lay side by side.
bool ChoiceStepper(const char* id, float x1, float cy, const std::vector<std::string>& options, int& sel) {
    const int n = (int)options.size();
    if (n == 0) return false;
    float widest = 0.f;
    for (const auto& o : options) widest = std::max(widest, TextW(Face::Medium, 12.f, o.c_str()));
    const float total = 2.f + 26.f + std::max(96.f, widest + 20.f) + 26.f + 2.f;
    const R box{x1 - total, cy - kSegH * 0.5f, total, kSegH};
    Fill(box, Hex(tok::kWell), 7.f);
    bool changed = false;
    for (int k = 0; k < 2; ++k) {
        char bid[128];
        std::snprintf(bid, sizeof bid, "%s#%s", id, k ? "next" : "prev");
        const R b{k ? box.x1() - 2.f - 26.f : box.x + 2.f, box.y + 2.f, 26.f, kSegH - 4.f};
        const Hit h = Item(bid, b);
        PointerOnHover(h);
        if (h.hovered) Fill(b, White(0.05f), 5.5f);
        if (h.clicked) {
            sel = ((std::clamp(sel, 0, n - 1) + (k ? 1 : -1)) % n + n) % n;
            changed = true;
        }
        DrawIcon(k ? Icon::ChevronRight : Icon::ChevronLeft, b.cx(), b.cy(), 13.f, Hex(h.hovered ? tok::kText : tok::kMuted));
    }
    TextMidC(Face::Medium, 12.f, box.cx(), box.cy(), AccentCol(), options[(size_t)std::clamp(sel, 0, n - 1)].c_str());
    return changed;
}

// Keycaps and binds.
float KeyCapWidth(const char* label, float size) { return std::max(20.f, 12.f + TextW(Face::MonoMedium, size, label)); }
void KeyCap(float x, float cy, const char* label, bool lit, float size) {
    const float w = KeyCapWidth(label, size), h = size + 8.5f;
    const R r{x, cy - h * 0.5f, w, h};
    Fill(r, White(0.075f), 4.5f);
    Fill(R{r.x + 1.f, r.y1() - 2.f, r.w - 2.f, 1.5f}, Black(0.35f), 1.f);   // the key's lower lip
    TextMidC(Face::MonoMedium, size, r.cx(), cy - 0.5f, lit ? AccentCol() : Hex(tok::kLabel), label);
}
float BindButtonWidth(const std::string& bind, bool capturing) {
    if (capturing) return 20.f + TextW(Face::Medium, 12.f, "Press a key");
    if (bind.empty()) return 16.f + 16.f + TextW(Face::Medium, 12.f, "Bind");
    return KeyCapWidth(bind.c_str(), 11.f);
}
Hit BindButton(const char* id, float x1, float cy, const std::string& bind, bool capturing) {
    const float w = BindButtonWidth(bind, capturing);
    const R r{x1 - w, cy - 13.f, w, 26.f};
    const Hit h = Item(id, r);
    PointerOnHover(h);
    if (capturing) {
        const float pulse = 0.55f + 0.45f * (0.5f + 0.5f * sinf((float)g_now * 6.f));
        Fill(r, AccentCol(0.12f), 7.f);
        TextMidC(Face::Medium, 12.f, r.cx(), cy, AccentCol(pulse), "Press a key");
    } else if (bind.empty()) {
        if (h.hovered) Fill(r, White(0.06f), 7.f);
        const Col c = Hex(h.hovered ? tok::kText : tok::kMuted);
        DrawIcon(Icon::Keyboard, r.x + 16.f, cy, 14.f, c);
        TextMid(Face::Medium, 12.f, r.x + 30.f, cy, c, "Bind");
    } else {
        KeyCap(r.x, cy, bind.c_str(), h.hovered, 11.f);
    }
    return h;
}

// Buttons.
float ButtonWidth(const char* label) { return 28.f + TextW(Face::SemiBold, 12.5f, label); }
Hit Button(const char* id, const R& r, const char* label, ButtonKind kind) {
    const Hit h = Item(id, r);
    PointerOnHover(h);
    switch (kind) {
    case ButtonKind::Primary:
        Fill(r, h.hovered ? Mix(AccentCol(), White(1.f), 0.1f) : AccentCol(), 7.f);
        TextMidC(Face::SemiBold, 12.5f, r.cx(), r.cy(), OnAccentCol(), label);
        break;
    case ButtonKind::Quiet:
        Fill(r, White(h.hovered ? 0.1f : 0.06f), 7.f);
        TextMidC(Face::SemiBold, 12.5f, r.cx(), r.cy(), Hex(h.hovered ? tok::kText : tok::kLabel), label);
        break;
    case ButtonKind::Ghost:
        if (h.hovered) Fill(r, White(0.05f), 7.f);
        TextMidC(Face::SemiBold, 12.5f, r.cx(), r.cy(), Hex(h.hovered ? tok::kText : tok::kMuted), label);
        break;
    case ButtonKind::Danger:
        Fill(r, Hex(tok::kDanger, h.hovered ? 0.26f : 0.16f), 7.f);
        TextMidC(Face::SemiBold, 12.5f, r.cx(), r.cy(), Hex(h.hovered ? 0xffd5d5 : tok::kDanger), label);
        break;
    }
    return h;
}

namespace {
ImTextureID g_iconTex = (ImTextureID)0;
}
void SetIconTexture(ImTextureID tex) { g_iconTex = tex; }

// A whole number of device pixels, on whole pixels, so an icon never shimmers.
void DrawIcon(Icon icon, float cx, float cy, float size, Col c) {
    const int n = (int)icon;
    if (!g_iconTex || n < 0 || n >= kIconCount || ((c >> IM_COL32_A_SHIFT) & 255) == 0) return;
    const float stride = (float)(kIconCell + 2 * kIconGutter);
    const float u0 = ((n % kIconCols) * stride + kIconGutter) / (float)kIconAtlasW;
    const float v0 = ((n / kIconCols) * stride + kIconGutter) / (float)kIconAtlasH;
    const float u1 = u0 + kIconCell / (float)kIconAtlasW, v1 = v0 + kIconCell / (float)kIconAtlasH;
    const float side = std::max(1.f, floorf(Dv(size) + 0.5f));
    const float x0 = floorf(Dv(cx) - side * 0.5f + 0.5f), y0 = floorf(Dv(cy) - side * 0.5f + 0.5f);
    g_dl->AddImage(g_iconTex, ImVec2(x0, y0), ImVec2(x0 + side, y0 + side), ImVec2(u0, v0), ImVec2(u1, v1), c);
}

Hit IconButton(const char* id, const R& r, Icon icon, float iconSize, bool danger) {
    const Hit h = Item(id, r);
    PointerOnHover(h);
    if (h.hovered) Fill(r, White(0.06f), 8.f);
    DrawIcon(icon, r.cx(), r.cy(), iconSize, h.hovered ? (danger ? Hex(tok::kDanger) : Hex(tok::kText)) : Hex(tok::kMuted));
    return h;
}

bool TextField(const char* id, const R& field, const std::string& text, const char* placeholder, std::string& out) {
    const ImGuiID target = Id(id);
    if (TakeCommit(EditKind::Text, target, out)) return true;
    if (EditingTarget(EditKind::Text, target)) {
        EditField(field, Face::Regular, 13.f, 10.f);
        return false;
    }
    const Hit h = Item(id, field);
    if (h.hovered) Cursor(ImGuiMouseCursor_TextInput);
    if (h.clicked) BeginEdit(EditKind::Text, target, text);
    Fill(field, Hex(tok::kWell), 7.f);
    Stroke(field, White(h.hovered ? 0.12f : 0.07f), 7.f, 1.f);
    const std::string shown = text.empty() ? std::string(placeholder) : Ellipsize(Face::Regular, 13.f, text, field.w - 20.f);
    TextMid(Face::Regular, 13.f, field.x + 10.f, field.cy(), Hex(text.empty() ? tok::kMuted : tok::kText), shown.c_str());
    return false;
}

// The scrollbar. Taken anywhere on the track the thumb comes to the pointer; taken by the thumb it
// keeps its grip.
ScrollBarState ScrollBarBegin(const char* id, const R& area, float& scroll, float& target, float content) {
    ScrollBarState s;
    s.id = id;
    s.area = area;
    const float maxScroll = content - area.h;
    if (maxScroll <= 0.5f) return s;
    s.visible = true;
    const R track{area.x1() - 11.f, area.y + 4.f, 11.f, area.h - 8.f};
    s.thumbH = std::max(28.f, track.h * area.h / content);
    const float room = std::max(1.f, track.h - s.thumbH);
    const Hit h = Item(id, track);
    static std::unordered_map<ImGuiID, float> grabs;
    const float my = Mouse().y;
    const float thumbY = track.y + room * std::clamp(scroll / maxScroll, 0.f, 1.f);
    if (h.activated) grabs[h.id] = (my >= thumbY && my < thumbY + s.thumbH) ? my - thumbY : s.thumbH * 0.5f;
    if (h.held) target = (std::clamp(my - grabs[h.id], track.y, track.y + room) - track.y) / room * maxScroll;
    s.thumbY = track.y + room * std::clamp((h.held ? target : scroll) / maxScroll, 0.f, 1.f);
    s.hot = h.hovered || h.held;
    s.held = h.held;
    return s;
}
void ScrollBarEnd(const ScrollBarState& s) {
    if (!s.visible) return;
    const float w = Spring(Id(s.id, 21), s.hot ? 6.f : 3.f, 32.f, 0.9f);
    const float a = Ease(Id(s.id, 22), s.held ? 0.36f : s.hot ? 0.25f : 0.13f, 0.12f);
    Fill(R{s.area.x1() - 2.5f - w, s.thumbY, w, s.thumbH}, White(a), w * 0.5f);
}

// Colour.
// The targets meet in the middle of each gap and stand as tall as the row, so there is nothing to miss
// between two colours, and a colour is taken as the button goes down: a small target is easy to slide
// off before the release.
bool Swatches(const char* id, float x1, float cy, const unsigned* colours, int n, unsigned& value, float d) {
    const float gap = 8.f;
    float x = x1 - (float)n * d - (float)(n - 1) * gap;
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        char sid[128];
        std::snprintf(sid, sizeof sid, "%s#%d", id, i);
        const float cx = x + d * 0.5f;
        const Hit h = Item(sid, R{x - gap * 0.5f, cy - 15.f, d + gap, 30.f});
        PointerOnHover(h);
        if (h.activated && value != colours[i]) {
            value = colours[i];
            changed = true;
        }
        const float ring = Ease(Id(sid, 3), value == colours[i] ? 1.f : (h.hovered ? 0.4f : 0.f), 0.14f);
        if (ring > 0.f) Ring(cx, cy, d * 0.5f + 3.5f, Hex(tok::kText, ring), 1.5f);
        Circle(cx, cy, d * 0.5f, Hex(colours[i]));
        x += d + gap;
    }
    return changed;
}

namespace {
constexpr float kSvH = 120.f, kHueH = 12.f, kPickGap = 10.f;

unsigned HsvRgb(float h, float s, float v) {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    auto c = [](float f) { return (unsigned)std::clamp((int)lroundf(f * 255.f), 0, 255); };
    return c(r) << 16 | c(g) << 8 | c(b);
}

// The knobs' hue, saturation and value: a grey has no hue and black no saturation, so those stay
// where they were left instead of jumping to red.
struct Hsv { float h = 0.f, s = 0.f, v = 0.f; unsigned rgb = 0xFFFFFFFFu; };
std::unordered_map<ImGuiID, Hsv> g_hsv;
Hsv& HsvOf(ImGuiID id, unsigned rgb) {
    Hsv& c = g_hsv[id];
    if (c.rgb != rgb) {
        float h, s, v;
        ImGui::ColorConvertRGBtoHSV(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, h, s, v);
        if (v > 0.f) {
            if (s > 0.f) c.h = h;
            c.s = s;
        }
        c.v = v;
        c.rgb = rgb;
    }
    return c;
}

void PickerKnob(float cx, float cy, float r, unsigned fill) {
    ShadowCircle(cx, cy, r, 1.f, 4.f, 0.35f);
    Circle(cx, cy, r, White(1.f));
    Circle(cx, cy, r - 2.f, Hex(fill));
}
}  // namespace

// The swatch that stands for "any colour": the hues round a ring.
void HueRing(float cx, float cy, float r) {
    const int n = 36;
    for (int i = 0; i < n; ++i) {
        const float a0 = 6.2831853f * (float)i / (float)n, a1 = 6.2831853f * (float)(i + 1) / (float)n + 0.02f;
        g_dl->PathClear();
        g_dl->PathArcTo(P(cx, cy), Dv(r - 1.75f), a0, a1, 4);
        g_dl->PathStroke(Hex(HsvRgb((float)i / (float)n, 0.85f, 1.f)), 0, Dv(3.5f));
    }
}

float PickerHeight() { return kSvH + kPickGap + kHueH; }

// Saturation across and value down, the hue under it.
bool ColourPicker(const char* id, float x, float y, float w, unsigned& rgb) {
    char sid[128];
    bool changed = false;
    Hsv& c = HsvOf(Id(id), rgb & 0xFFFFFF);
    auto set = [&](unsigned v) {
        if ((rgb & 0xFFFFFF) == v) return;
        rgb = v;
        c.rgb = v;
        changed = true;
    };
    const R sv{x, y, w, kSvH};
    std::snprintf(sid, sizeof sid, "%s#sv", id);
    const Hit hsv = Item(sid, sv);
    PointerOnHover(hsv);
    const ImVec2 m = Mouse();
    if (hsv.held) {
        c.s = std::clamp((m.x - sv.x) / sv.w, 0.f, 1.f);
        c.v = 1.f - std::clamp((m.y - sv.y) / sv.h, 0.f, 1.f);
        set(HsvRgb(c.h, c.s, c.v));
    }
    const R hue{x, sv.y1() + kPickGap, w, kHueH};
    std::snprintf(sid, sizeof sid, "%s#hue", id);
    const Hit hh = Item(sid, R{hue.x, hue.y - 4.f, hue.w, hue.h + 8.f});
    PointerOnHover(hh);
    if (hh.held) {
        c.h = std::min(std::clamp((m.x - hue.x) / hue.w, 0.f, 1.f), 0.9999f);
        set(HsvRgb(c.h, c.s, c.v));
    }
    const ImU32 pure = Hex(HsvRgb(c.h, 1.f, 1.f));
    g_dl->AddRectFilledMultiColor(Min(sv), Max(sv), White(1.f), pure, pure, White(1.f));
    g_dl->AddRectFilledMultiColor(Min(sv), Max(sv), Black(0.f), Black(0.f), Black(1.f), Black(1.f));
    for (int i = 0; i < 6; ++i) {
        const float x0 = hue.x + hue.w * (float)i / 6.f, x1 = hue.x + hue.w * (float)(i + 1) / 6.f;
        const ImU32 l = Hex(HsvRgb((float)i / 6.f, 1.f, 1.f)), r = Hex(HsvRgb((float)((i + 1) % 6) / 6.f, 1.f, 1.f));
        g_dl->AddRectFilledMultiColor(P(x0, hue.y), P(x1, hue.y1()), l, r, r, l);
    }
    PickerKnob(sv.x + c.s * sv.w, sv.y + (1.f - c.v) * sv.h, 7.f, rgb & 0xFFFFFF);
    PickerKnob(hue.x + c.h * hue.w, hue.cy(), 8.f, HsvRgb(c.h, 1.f, 1.f));
    return changed;
}

void BeginFrame(ImDrawList* dl, float scale, float fontScale, unsigned accent, int surface) {
    g_dl = dl;
    g_s = scale;
    g_fk = fontScale > 0.f ? scale / fontScale : 1.f;
    g_now = ImGui::GetTime();
    g_dt = std::clamp(ImGui::GetIO().DeltaTime, 0.f, 0.5f);
    g_frame = ++g_frames[surface];
    g_accent = accent ? accent : tok::kLime;
    g_lightAccent = Luminance(g_accent) > 0.45f;
    g_cursor = ImGuiMouseCursor_Arrow;
    g_interactive = true;
    g_alpha.assign(1, 1.f);
    if (g_record) {
        g_items.swap(g_itemsBuilding);
        g_itemsBuilding.clear();
    }
}

}  // namespace citron::ui
