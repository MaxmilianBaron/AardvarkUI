#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <d3d9.h>
#include <memory>
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
    bool Load(IDirect3DDevice9 *, const wchar_t *family = L"Segoe UI", int pixels = 40);
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
    void AddRectFilled(Point min, Point max, Color);
    void AddLine(Point from, Point to, Color, float thickness = 1);
    void AddText(Font *, float size, Point position, Color, const char *, const char *end = nullptr,
                 float wrap = 0);
};

struct Input {
    Point DisplaySize, MousePos, MouseDelta;
    bool MouseDown[3]{};
    void AddMousePosEvent(float x, float y) {
        MousePos = {x, y};
    }
};

struct Context;
constexpr int NoInputs = 1, FirstUse = 1;
Context *CreateContext(HWND window = nullptr, IDirect3DDevice9 *device = nullptr);
void DestroyContext(Context *);
void SetCurrentContext(Context *);
Input &GetIO();
Font *LoadFont(const wchar_t *family = L"Segoe UI", int pixels = 40);
Font *GetFont();
void Message(HWND, UINT, WPARAM, LPARAM);
void NewFrame();
void Render();
void Flush();
void EndFrame();
void SetInputEnabled(bool);
void ClearInput();

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

void Text(const char *, float size = 18, Color color = RGBA(218, 225, 235), float wrap = 0);
bool Button(const char *label, Point size = {160, 38});
bool Checkbox(const char *label, bool &value);
bool SliderFloat(const char *label, float &value, float min, float max, float width = 240);

}
