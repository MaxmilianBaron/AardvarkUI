#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <d3d9.h>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

namespace aardvark::ui {

using Color = std::uint32_t;
using Texture = IDirect3DTexture9 *;
constexpr Color RGBA(unsigned r, unsigned g, unsigned b, unsigned a = 255) {
    return ((a & 255) << 24) | ((r & 255) << 16) | ((g & 255) << 8) | (b & 255);
}
constexpr Color White = 0xffffffffu;

struct Point {
    float x = 0, y = 0;
    Point() = default;
    Point(float a, float b) : x(a), y(b) {}
};
struct Rect {
    Point min, max;
};
struct Glyph {
    Texture texture = nullptr;
    Point uv0, uv1, offset{-2, 0};
    float advance = 0, width = 0, height = 0;
};

class Font {
    struct Data;
    std::unique_ptr<Data> data;

  public:
    Font();
    ~Font();
    Font(const Font &) = delete;
    Font &operator=(const Font &) = delete;
    bool Load(IDirect3DDevice9 *, const wchar_t *family = L"Cascadia Code", int pixels = 40);
    const Glyph &GetGlyph(std::uint32_t);
    Point MeasureText(const char *text, float size, float wrap = 0);
    float Height() const;
    float SizeForEm(float) const;
};

struct Vertex {
    float x, y, z, rhw;
    Color color;
    float u, v;
};
struct Command {
    Texture texture;
    Rect clip;
    std::size_t first, count;
};

class DrawList {
    std::vector<Rect> clips;

  public:
    std::vector<Vertex> vertices;
    std::vector<Command> commands;
    void Clear(Point display);
    void PushClipRect(Point min, Point max, bool intersect = true);
    void PopClipRect();
    void AddImage(Texture, Point min, Point max, Point uv0 = {}, Point uv1 = {1, 1}, Color = White);
    void AddRectFilled(Point min, Point max, Color, float rounding = 0);
    void AddRect(Point min, Point max, Color, float thickness = 1, float rounding = 0);
    void AddCircleFilled(Point center, float radius, Color);
    void AddLine(Point from, Point to, Color, float thickness = 1);
    void AddText(Font *, float size, Point position, Color, const char *, const char *end = nullptr,
                 float wrap = 0);
};

struct Input {
    Point DisplaySize, MousePos, MouseDelta;
    bool MouseDown[3]{};
    bool KeysDown[256]{};
    bool WantCaptureMouse = false, WantCaptureKeyboard = false, WantTextInput = false;
    void AddMousePosEvent(float x, float y) {
        MousePos = {x, y};
    }
};

struct Style {
    Color Text = RGBA(21, 25, 34), Muted = RGBA(104, 112, 128);
    Color Background = White, Field = RGBA(243, 245, 246);
    Color Button = RGBA(17, 24, 39), Hovered = RGBA(51, 65, 85), Active = RGBA(15, 23, 42);
    Color Accent = RGBA(19, 148, 71), Border = RGBA(221, 227, 232), Selection = RGBA(206, 240, 218);
    float FontSize = 17, Spacing = 8, Rounding = 6;
};

struct Context;
constexpr int NoInputs = 1, FirstUse = 1;
Context *CreateContext(HWND window = nullptr, IDirect3DDevice9 *device = nullptr);
void DestroyContext(Context *);
void SetCurrentContext(Context *);
Input &GetIO();
Style &GetStyle();
Font *LoadFont(const wchar_t *family = L"Cascadia Code", int pixels = 40);
Font *GetFont();
void Message(HWND, UINT, WPARAM, LPARAM);
void NewFrame();
void Render();
void Flush();
void EndFrame();
void SetInputEnabled(bool);
void ClearInput();
bool IsKeyPressed(unsigned key);
bool IsItemFocused();
void SetKeyboardFocusHere();
using ClipboardRead = bool (*)(std::string &, void *);
using ClipboardWrite = bool (*)(const std::string &, void *);
void SetClipboardHandlers(ClipboardRead, ClipboardWrite, void *user = nullptr);

void SetNextWindowPos(Point, int condition = 0, Point pivot = {});
void SetNextWindowSize(Point);
void SetWindowPos(Point);
Point GetWindowPos();
Point GetWindowSize();
bool Begin(const char *name, int flags = 0);
void End();
bool BeginChild(const char *name, Point size, int flags = 0);
void EndChild();
void BeginDisabled(bool disabled = true);
void EndDisabled();
void PushID(const char *);
void PushID(int);
void PopID();
DrawList *GetWindowDrawList();
DrawList *GetForegroundDrawList();
Point GetCursorScreenPos();
void SetCursorScreenPos(Point);
Point GetItemRectMin();
Point GetItemRectMax();
bool InvisibleButton(const char *id, Point size);
bool IsItemActivated();
bool IsItemActive();
bool IsItemHovered();
bool IsWindowHovered();
bool IsMouseHoveringRect(Point min, Point max);
bool IsMouseDragging(int button, float threshold = 0);
bool IsMouseDoubleClicked(int button);
void Dummy(Point);
void SameLine(float spacing = -1);
void Spacing(float height = -1);
void Separator(float width = 0);

bool BeginTable(const char *id, int columns, float width = 0);
void TableNextColumn();
void EndTable();
void OpenPopup(const char *id);
bool BeginPopup(const char *id, Point size);
void EndPopup();
void CloseCurrentPopup();

void Text(const char *, float size = 18, Color color = 0, float wrap = 0);
bool Button(const char *label, Point size = {160, 38});
bool Checkbox(const char *label, bool &value);
bool SliderFloat(const char *label, float &value, float min, float max, float width = 240);
enum TextFlags { TextReadOnly = 1, TextPassword = 2, TextEnterReturnsTrue = 4 };
bool InputText(const char *id, std::string &value, float width = 240, std::size_t max_bytes = 4096,
               int flags = 0);
bool Selectable(const char *label, bool selected = false, Point size = {240, 30});
bool RadioButton(const char *label, int &value, int option);
bool CollapsingHeader(const char *label, bool &open, float width = 240);
bool Combo(const char *label, int &selected, const char *const *items, int count, float width = 240);
void ProgressBar(float fraction, Point size = {240, 20}, const char *overlay = nullptr);

}
