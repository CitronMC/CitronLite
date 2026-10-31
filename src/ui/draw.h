// The panel's drawing kit, in the design language of the full client. Layout is in design pixels and
// every coordinate is scaled once, when it reaches the draw list, so the numbers in the layout code
// stay the numbers of the design. Controls are invisible buttons with their own drawing: ImGui gives
// the hover to the first item under the pointer, so a control inside a clickable area is submitted
// before its container.
#pragma once
#include "imgui.h"
#include <string>
#include <vector>

namespace citron::ui {

constexpr float kPanelW = 920.f, kPanelH = 600.f, kPanelRadius = 14.f;

namespace tok {
inline constexpr unsigned kPanel = 0x121411;     // the window
inline constexpr unsigned kCard = 0x1a1c18;      // raised: the detail card, the selected row
inline constexpr unsigned kCardHover = 0x1f221d;
inline constexpr unsigned kWell = 0x0e0f0c;      // sunken: inputs, segmented tracks
inline constexpr unsigned kText = 0xeceee6;      // names, values
inline constexpr unsigned kLabel = 0xbcc1b3;     // setting labels
inline constexpr unsigned kIdle = 0x9da394;      // names of modules that are off
inline constexpr unsigned kSub = 0x8b9181;       // units, secondary numbers
inline constexpr unsigned kMuted = 0x6c7264;     // descriptions, inactive tabs
inline constexpr unsigned kFaint = 0x4a4f44;     // quiet numbers
inline constexpr unsigned kLime = 0xc8f135;      // the accent
inline constexpr unsigned kOnLime = 0x141905;    // what is drawn on the accent
inline constexpr unsigned kAmber = 0xf2b544;     // attention without error
inline constexpr unsigned kDanger = 0xff6b6b;
}  // namespace tok

extern const unsigned kSwatches[7];
extern float g_s;
extern double g_now;

using Col = ImU32;
Col Hex(unsigned rgb, float a = 1.f);
Col White(float a);
Col Black(float a);
Col Mix(Col a, Col b, float t);
Col AccentCol(float a = 1.f);
Col OnAccentCol();
void PushAlpha(float a);
void PopAlpha();

struct R {
    float x = 0, y = 0, w = 0, h = 0;
    float x1() const { return x + w; }
    float y1() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    bool contains(float px, float py) const { return px >= x && px < x1() && py >= y && py < y1(); }
    R Inset(float d) const { return {x + d, y + d, w - 2.f * d, h - 2.f * d}; }
};
ImVec2 Min(const R& r);
ImVec2 Max(const R& r);
ImVec2 Mouse();                 // the pointer in design pixels
ImDrawList* DL();

void Fill(const R& r, Col c, float radius = 0.f);
void Stroke(const R& r, Col c, float radius, float thickness);
void Circle(float cx, float cy, float r, Col c);
void Ring(float cx, float cy, float r, Col c, float thickness);
void Line(float x0, float y0, float x1, float y1, Col c, float thickness);
void Shadow(const R& r, float radius, float offY, float blur, float alpha);
void PushClip(const R& r);
void PopClip();

// Hanken Grotesk for the interface, Schibsted Grotesk for titles, JetBrains Mono for numbers and keys.
// Sizes are em sizes in design pixels.
enum class Face { Regular, Medium, SemiBold, Title, TitleBold, MonoMedium, Count };
struct FontFile { const void* data; int size; };
void BuildFonts(ImFontAtlas* atlas, const FontFile (&files)[(int)Face::Count], float scale);
float TextW(Face f, float size, const char* s, float tracking = 0.f);
float CapH(Face f, float size);
void Text(Face f, float size, float x, float baseline, Col c, const char* s, float tracking = 0.f);
// Centred on the cap height at cy; the variants anchor the right edge or the middle.
void TextMid(Face f, float size, float x, float cy, Col c, const char* s, float tracking = 0.f);
void TextMidR(Face f, float size, float x1, float cy, Col c, const char* s, float tracking = 0.f);
void TextMidC(Face f, float size, float cx, float cy, Col c, const char* s, float tracking = 0.f);
std::string Ellipsize(Face f, float size, const std::string& s, float maxW);

float Dt();
float Ease(ImGuiID id, float target, float dur);
float Spring(ImGuiID id, float target, float omega = 26.f, float zeta = 0.78f);
void SnapSpring(ImGuiID id, float value);
float Approach(float current, float target, float rate = 14.f);
float EaseOutT(float t);
ImGuiID Id(const char* s, int n = 0);

struct Hit { bool hovered = false, clicked = false, held = false, activated = false; ImGuiID id = 0; };
Hit Item(const char* id, const R& r);
void Cursor(ImGuiMouseCursor c);
void PointerOnHover(const Hit& h);
void SetInteractive(bool on);
ImGuiMouseCursor FrameCursor();
// With recording on, each item's rectangle is kept under its id for the frame after, so a session can
// click a control by name. Note keeps one that is not an item, such as a card.
void RecordItems(bool on);
void Note(const char* id, const R& r);
bool ItemRect(const std::string& id, R& out);
std::vector<std::pair<std::string, R>> ItemRects();

// The inline editor behind every text field.
enum class EditKind { None, Rename, Value, Text };
void BeginEdit(EditKind kind, ImGuiID target, const std::string& text);
void EndEdit();
bool Editing();
bool EditingTarget(EditKind kind, ImGuiID target);
void ProcessEditor(ImGuiIO& io);
void EditField(const R& field, Face f, float size, float pad);
bool TakeCommit(EditKind kind, ImGuiID target, std::string& text);
void EndFrameEditor();

bool Switch(const char* id, const R& r, bool& on);
bool Checkbox(const char* id, float x, float cy, float maxW, const char* label, bool& on);
// One line: label, track, value. `labelW` is the label's column; click the number to type one.
bool Slider(const char* id, const R& row, const char* label, float labelW, float& v, float mn, float mx, float step, const char* unit);
bool RangeSlider(const char* id, const R& row, const char* label, float labelW, float& lo, float& hi, float mn, float mx, float step,
                 const char* unit);
// `dots` marks options with a small light, one flag an option.
// Label and value over the track, for a narrow column.
bool StackSlider(const char* id, const R& row, const char* label, float& v, float mn, float mx, float step, const char* unit);
bool StackRange(const char* id, const R& row, const char* label, float& lo, float& hi, float mn, float mx, float step, const char* unit);
bool Segmented(const char* id, float x1, float cy, const std::vector<std::string>& options, int& sel, const bool* dots = nullptr);
float SegmentedWidth(const std::vector<std::string>& options);
bool ChoiceStepper(const char* id, float x1, float cy, const std::vector<std::string>& options, int& sel);
float KeyCapWidth(const char* label, float size = 10.5f);
void KeyCap(float x, float cy, const char* label, bool lit = false, float size = 10.5f);
// A keycap when bound, "Bind" when not, "Press a key" while capturing.
Hit BindButton(const char* id, float x1, float cy, const std::string& bind, bool capturing);
float BindButtonWidth(const std::string& bind, bool capturing);
enum class ButtonKind { Primary, Quiet, Ghost, Danger };
Hit Button(const char* id, const R& r, const char* label, ButtonKind kind);
float ButtonWidth(const char* label);
enum class Icon { Close, Minimize, Keyboard, Check, ChevronLeft, ChevronRight, Pencil, Trash, ChevronDown };
void DrawIcon(Icon i, float cx, float cy, float size, Col c);
void SetIconTexture(ImTextureID tex);
Hit IconButton(const char* id, const R& r, Icon icon, float iconSize = 14.f, bool danger = false);
// A field that keeps nothing: true once with what was typed, on Enter or a click elsewhere.
bool TextField(const char* id, const R& field, const std::string& text, const char* placeholder, std::string& out);

// A scrollbar at the right edge of `area` whose thumb can be dragged. Hidden when the content fits.
// Begin before drawing the content, End after it.
struct ScrollBarState {
    bool visible = false, hot = false, held = false;
    R area;
    float thumbY = 0.f, thumbH = 0.f;
    const char* id = "";
};
ScrollBarState ScrollBarBegin(const char* id, const R& area, float& scroll, float& target, float content);
void ScrollBarEnd(const ScrollBarState& s);

bool Swatches(const char* id, float x1, float cy, const unsigned* colours, int n, unsigned& value, float d = 15.f);
void HueRing(float cx, float cy, float r);
float PickerHeight();
bool ColourPicker(const char* id, float x, float y, float w, unsigned& rgb);

// Every frame, before anything is drawn. `surface` is which window it is: each keeps its own frame count,
// so an animation on one is not taken for gone while the other draws.
void BeginFrame(ImDrawList* dl, float scale, float fontScale, unsigned accent, int surface = 0);

}  // namespace citron::ui
