// The panel's drawing kit. Layout is in design pixels (the panel is 860 x 580) and every
// coordinate is scaled once, when it reaches the draw list, so the numbers in the layout code
// stay the numbers of the design. Nothing here is a stock ImGui widget: controls are invisible
// buttons with their own drawing.
#pragma once
#include "imgui.h"
#include <string>
#include <vector>

namespace citron::ui {

constexpr float kPanelW = 860.f, kPanelH = 580.f;

// colours
constexpr unsigned kText = 0xe8eaf0, kMuted = 0x6a7080, kLabel = 0xb8bcc8, kBindText = 0x8a90a0;
constexpr unsigned kPanelBg = 0x111318, kCardBg = 0x171a21, kDanger = 0xff6b7a, kWarn = 0xfbbf24;
constexpr unsigned kDefaultAccent = 0xc8f135;
extern const unsigned kSwatches[6];

extern unsigned g_accent;
extern float g_alpha;
extern float g_s, g_fk;
extern double g_now;
extern bool g_interactive;
extern ImGuiMouseCursor g_cursor;

ImU32 Hex(unsigned rgb, float a = 1.f);
ImU32 White(float a);
ImU32 Black(float a);
ImU32 Mix(ImU32 a, ImU32 b, float t);
ImU32 Brighten(ImU32 c, float k);
ImU32 Accent(float a = 1.f);
ImU32 OnAccent();
bool LightAccent();
unsigned HsvToRgb(float h, float s, float v);

// fonts: Albert Sans 400/600/700 at the sizes the layout uses
struct FontData { const void* data; int size; };
void BuildFonts(ImFontAtlas* atlas, FontData regular, FontData semibold, FontData bold, float scale);
ImFont* F(int weight, float size);
struct LineM { float asc, desc, height; };
LineM LM(float size);

struct R {
    float x = 0, y = 0, w = 0, h = 0;
    float x1() const { return x + w; }
    float y1() const { return y + h; }
    bool contains(float px, float py) const { return px >= x && px < x1() && py >= y && py < y1(); }
};
float Dv(float v);
ImVec2 Min(const R& r);
ImVec2 Max(const R& r);
void Fill(ImDrawList* dl, const R& r, ImU32 col, float rounding = 0.f);
void Stroke(ImDrawList* dl, const R& r, ImU32 col, float rounding, float thickness);
void Circle(ImDrawList* dl, float cx, float cy, float r, ImU32 col);
void Line(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 col, float thickness);
void PushClip(const R& r);
void PopClip();

float TextW(ImFont* f, const char* s, float spacing = 0.f);
void TextAt(ImDrawList* dl, ImFont* f, float x, float baseline, ImU32 col, const char* s, float spacing = 0.f);
void TextRight(ImDrawList* dl, ImFont* f, float x1, float baseline, ImU32 col, const char* s, float spacing = 0.f);
void TextCenter(ImDrawList* dl, ImFont* f, float cx, float baseline, ImU32 col, const char* s, float spacing = 0.f);
std::string Ellipsize(ImFont* f, const std::string& s, float maxW);

void ShadowRect(ImDrawList* dl, const R& r, float rounding, float offY, float blur, float alpha);
float Animate(ImGuiID id, float target, float dur);
float SmoothTo(float current, float target, float dt);
float EaseCss(float t);

struct Hit { bool hovered = false, clicked = false, active = false, activated = false; ImGuiID id = 0; };
Hit Item(const char* id, const R& r);
void Pointer(const Hit& h);

// The inline editor: config names, typed slider values and text fields share it.
enum class EditKind { None, Rename, Value, Text };
void BeginEdit(EditKind kind, ImGuiID target, const std::string& text);
void EndEdit();
bool Editing();
bool EditingTarget(EditKind kind, ImGuiID target);
void ProcessEditor(ImGuiIO& io);
void EditFieldHit(const R& field);
void DrawEditField(ImDrawList* dl, const R& field, ImFont* f, float pad);
bool TakeCommit(EditKind kind, ImGuiID target, std::string& text);
void EndFrameEditor();

// widgets
void Knob(ImDrawList* dl, float cx, float cy, float rad, float blur, float alpha, bool rim);
void DrawSwitch(ImDrawList* dl, const R& r, bool on, const char* animKey);
bool SwitchRow(ImDrawList* dl, const char* id, const R& r, const char* label, bool& on);
float BindButtonWidth(const std::string& label);
Hit BindButton(ImDrawList* dl, const char* id, const R& r, const std::string& label, bool capturing);
Hit TextButton(ImDrawList* dl, const char* id, const R& r, const char* label, bool primary);
float TextButtonWidth(const char* label);
std::string NumberText(float v, float step);
void SliderUI(ImDrawList* dl, const char* id, const R& r, const char* label, float& v, float mn, float mx, float step,
              const char* unit);
void RangeUI(ImDrawList* dl, const char* id, const R& r, const char* label, float& lo, float& hi, float mn, float mx,
             float step, const char* unit);
void SegmentedUI(ImDrawList* dl, const char* prefix, float x1, float y, const std::vector<std::string>& options, int& selected);
float SegmentedWidth(const std::vector<std::string>& options);
void StepperUI(ImDrawList* dl, const char* prefix, float x1, float y, const std::vector<std::string>& options, int& selected);
bool TextField(ImDrawList* dl, const char* id, const R& field, const std::string& text, const char* placeholder,
               std::string& out);
void DrawCross(ImDrawList* dl, float cx, float cy, float half, ImU32 col, float thickness);
void DrawPencil(ImDrawList* dl, float cx, float cy, ImU32 col);
void DrawChevron(ImDrawList* dl, float cx, float cy, float half, bool right, ImU32 col);
void SelectionRing(ImDrawList* dl, float cx, float cy, float rad, unsigned bg);
void DrawHueWheel(ImDrawList* dl, float cx, float cy, float rad);

// Every frame, before anything is drawn: the scale, the clock, the accent.
void BeginFrame(float scale, float fontScale, unsigned accent);

}  // namespace citron::ui
