#include <aardvark/ui.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <windowsx.h>

namespace aardvark::ui {

static uint32_t NextCodepoint(const char *&text) {
    const auto lead = static_cast<unsigned char>(*text++);
    if (lead < 128)
        return lead;
    unsigned count = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : lead >= 0xc2 ? 1 : 0;
    if (!count || lead > 0xf4)
        return 0xfffd;
    uint32_t value = lead & ((1u << (6 - count)) - 1);
    for (unsigned i = 0; i < count; ++i) {
        const auto byte = static_cast<unsigned char>(*text);
        if ((byte & 0xc0) != 0x80)
            return 0xfffd;
        ++text;
        value = (value << 6) | (byte & 63);
    }
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) || (count == 1 && value < 128) ||
        (count == 2 && value < 2048) || (count == 3 && value < 65536))
        return 0xfffd;
    return value;
}

struct Font::Data {
    IDirect3DDevice9 *device = nullptr;
    HDC dc = nullptr;
    HFONT font = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldFont = nullptr, oldBitmap = nullptr;
    uint32_t *pixels = nullptr;
    float height = 64, emHeight = 64;
    unsigned slot = 0;
    std::vector<IDirect3DTexture9 *> pages;
    std::map<uint32_t, Glyph> glyphs;
    ~Data() {
        for (auto *texture : pages)
            texture->Release();
        if (dc) {
            if (oldFont)
                SelectObject(dc, oldFont);
            if (oldBitmap)
                SelectObject(dc, oldBitmap);
        }
        if (font)
            DeleteObject(font);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        if (device)
            device->Release();
    }
};
Font::Font() : data(std::make_unique<Data>()) {}
Font::~Font() = default;
bool Font::Load(IDirect3DDevice9 *device, const wchar_t *family, int pixels) {
    if (!device || !family || !*family || pixels < 8 || pixels > 80 || data->device)
        return false;
    auto next = std::make_unique<Data>();
    next->device = device;
    device->AddRef();
    next->dc = CreateCompatibleDC(nullptr);
    next->font =
        CreateFontW(-pixels, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
                    CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, family);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 128;
    info.bmiHeader.biHeight = -128;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    if (next->dc)
        next->bitmap = CreateDIBSection(next->dc, &info, DIB_RGB_COLORS,
                                        reinterpret_cast<void **>(&next->pixels), nullptr, 0);
    if (!next->dc || !next->font || !next->bitmap)
        return false;
    next->oldFont = SelectObject(next->dc, next->font);
    next->oldBitmap = SelectObject(next->dc, next->bitmap);
    TEXTMETRICW metrics{};
    if (!GetTextMetricsW(next->dc, &metrics) || metrics.tmHeight <= 0 || metrics.tmHeight > 120 ||
        metrics.tmInternalLeading < 0 || metrics.tmInternalLeading >= metrics.tmHeight)
        return false;
    next->height = static_cast<float>(metrics.tmHeight);
    next->emHeight = static_cast<float>(metrics.tmHeight - metrics.tmInternalLeading);
    SetTextColor(next->dc, RGB(255, 255, 255));
    SetBkColor(next->dc, 0);
    SetBkMode(next->dc, OPAQUE);
    data = std::move(next);
    return true;
}
float Font::Height() const {
    return data->height;
}
float Font::SizeForEm(float size) const {
    return size * data->height / data->emHeight;
}
const Glyph &Font::GetGlyph(uint32_t code) {
    if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
        code = 0xfffd;
    const auto found = data->glyphs.find(code);
    if (found != data->glyphs.end())
        return found->second;
    if (data->glyphs.size() >= 1023 && code != 0xfffd)
        return GetGlyph(0xfffd);
    Glyph glyph;
    wchar_t chars[2]{};
    unsigned length = 1;
    if (code > 65535) {
        code -= 65536;
        chars[0] = static_cast<wchar_t>(0xd800 + (code >> 10));
        chars[1] = static_cast<wchar_t>(0xdc00 + (code & 1023));
        code += 65536;
        length = 2;
    } else
        chars[0] = static_cast<wchar_t>(code);
    SIZE extent{};
    if (!GetTextExtentPoint32W(data->dc, chars, length, &extent))
        return data->glyphs.emplace(code, glyph).first->second;
    glyph.advance = static_cast<float>(std::clamp(extent.cx, 0L, 120L));
    const unsigned index = data->slot / 256, slot = data->slot % 256;
    if (index >= data->pages.size()) {
        IDirect3DTexture9 *page = nullptr;
        if (FAILED(data->device->CreateTexture(2048, 2048, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &page,
                                               nullptr)))
            return data->glyphs.emplace(code, glyph).first->second;
        D3DLOCKED_RECT lock{};
        if (FAILED(page->LockRect(0, &lock, nullptr, 0))) {
            page->Release();
            return data->glyphs.emplace(code, glyph).first->second;
        }
        for (unsigned y = 0; y < 2048; ++y)
            std::memset(static_cast<unsigned char *>(lock.pBits) + y * lock.Pitch, 0, 2048 * 4);
        page->UnlockRect(0);
        const auto release = [](IDirect3DTexture9 *texture) { texture->Release(); };
        std::unique_ptr<IDirect3DTexture9, decltype(release)> owner(page, release);
        data->pages.push_back(page);
        owner.release();
    }
    std::memset(data->pixels, 0, 128 * 128 * 4);
    RECT box{0, 0, 128, 128};
    ExtTextOutW(data->dc, 2, 0, ETO_CLIPPED | ETO_OPAQUE, &box, chars, length, nullptr);
    GdiFlush();
    const LONG x = static_cast<LONG>(slot % 16) * 128, y = static_cast<LONG>(slot / 16) * 128;
    RECT area{x, y, x + 128, y + 128};
    D3DLOCKED_RECT lock{};
    auto *page = data->pages[index];
    if (FAILED(page->LockRect(0, &lock, &area, 0)))
        return data->glyphs.emplace(code, glyph).first->second;
    for (unsigned row = 0; row < 128; ++row) {
        auto *target =
            reinterpret_cast<uint32_t *>(static_cast<unsigned char *>(lock.pBits) + row * lock.Pitch);
        for (unsigned col = 0; col < 128; ++col) {
            const uint32_t pixel = data->pixels[row * 128 + col];
            const uint32_t alpha = std::max({pixel & 255, (pixel >> 8) & 255, (pixel >> 16) & 255});
            target[col] = (alpha << 24) | 0x00ffffff;
        }
    }
    page->UnlockRect(0);
    ++data->slot;
    glyph.texture = page;
    glyph.width = glyph.advance + 4;
    glyph.height = data->height;
    glyph.uv0 = {x / 2048.0f, y / 2048.0f};
    glyph.uv1 = {(x + glyph.width) / 2048.0f, (y + glyph.height) / 2048.0f};
    return data->glyphs.emplace(code, glyph).first->second;
}

template <class Emit>
static Point LayoutText(Font &font, float size, float wrap, const char *text, Emit emit) {
    if (!std::isfinite(size) || size <= 0 || !std::isfinite(wrap))
        return {};
    float x = 0, y = 0, width = 0;
    const float factor = size / font.Height();
    const char *at = text;
    while (at && *at) {
        if (*at == '\n') {
            ++at;
            width = std::max(width, x);
            x = 0;
            y += size;
            continue;
        }
        if (wrap > 0 && *at != ' ' && *at != '\t') {
            const char *end = at;
            float word = 0;
            while (*end && *end != ' ' && *end != '\t' && *end != '\n')
                word += font.GetGlyph(NextCodepoint(end)).advance * factor;
            if (x > 0 && x + word > wrap) {
                width = std::max(width, x);
                x = 0;
                y += size;
            }
            while (at < end) {
                const auto &glyph = font.GetGlyph(NextCodepoint(at));
                if (wrap > 0 && x > 0 && x + glyph.advance * factor > wrap) {
                    width = std::max(width, x);
                    x = 0;
                    y += size;
                }
                emit(glyph, x, y, factor);
                x += glyph.advance * factor;
            }
        } else {
            const auto code = NextCodepoint(at);
            const auto &glyph = font.GetGlyph(code == '\t' ? ' ' : code);
            const float advance = glyph.advance * factor * (code == '\t' ? 4 : 1);
            if (wrap > 0 && x + advance > wrap) {
                width = std::max(width, x);
                x = 0;
                y += size;
            } else {
                emit(glyph, x, y, factor);
                x += advance;
            }
        }
    }
    return {std::max(width, x), text && *text ? y + size : 0};
}
Point Font::MeasureText(const char *text, float size, float wrap) {
    return LayoutText(*this, size, wrap, text, [](const Glyph &, float, float, float) {});
}
static bool Contains(Rect r, Point p) {
    return p.x >= r.min.x && p.y >= r.min.y && p.x < r.max.x && p.y < r.max.y;
}
static Rect Intersection(Rect a, Rect b) {
    return {{std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y)},
            {std::min(a.max.x, b.max.x), std::min(a.max.y, b.max.y)}};
}
void DrawList::Clear(Point display) {
    vertices.clear();
    commands.clear();
    clips = {{{0, 0}, display}};
}
void DrawList::PushClipRect(Point a, Point b, bool intersect) {
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y))
        a = b = {};
    Rect rect{a, b};
    if (intersect && !clips.empty())
        rect = Intersection(rect, clips.back());
    clips.push_back(rect);
}
void DrawList::PopClipRect() {
    if (clips.size() > 1)
        clips.pop_back();
}
void DrawList::AddImage(Texture texture, Point a, Point b, Point uv0, Point uv1, Color color) {
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y) ||
        !std::isfinite(uv0.x) || !std::isfinite(uv0.y) || !std::isfinite(uv1.x) || !std::isfinite(uv1.y))
        return;
    if (clips.empty() || b.x <= a.x || b.y <= a.y || !(color >> 24) || vertices.size() > 1000000)
        return;
    const auto clip = clips.back();
    if (b.x <= clip.min.x || b.y <= clip.min.y || a.x >= clip.max.x || a.y >= clip.max.y)
        return;
    const size_t first = vertices.size();
    Vertex corners[] = {{a.x - 0.5f, a.y - 0.5f, 0, 1, color, uv0.x, uv0.y},
                        {b.x - 0.5f, a.y - 0.5f, 0, 1, color, uv1.x, uv0.y},
                        {b.x - 0.5f, b.y - 0.5f, 0, 1, color, uv1.x, uv1.y},
                        {a.x - 0.5f, b.y - 0.5f, 0, 1, color, uv0.x, uv1.y}};
    for (unsigned i : {0u, 1u, 2u, 0u, 2u, 3u})
        vertices.push_back(corners[i]);
    if (!commands.empty() && commands.back().texture == texture &&
        std::memcmp(&commands.back().clip, &clip, sizeof(clip)) == 0)
        commands.back().count += 6;
    else
        commands.push_back({texture, clip, first, 6});
}
void DrawList::AddRectFilled(Point a, Point b, Color color) {
    AddImage(nullptr, a, b, {}, {1, 1}, color);
}
void DrawList::AddLine(Point a, Point b, Color color, float thickness) {
    if (clips.empty() || !(color >> 24) || vertices.size() > 1000000 || !std::isfinite(a.x) ||
        !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(thickness) ||
        thickness <= 0)
        return;
    const float dx = b.x - a.x, dy = b.y - a.y, length = std::sqrt(dx * dx + dy * dy);
    if (!std::isfinite(length) || length < 0.01f)
        return;
    const float nx = -dy * thickness / (2 * length), ny = dx * thickness / (2 * length);
    const size_t first = vertices.size();
    Vertex v[] = {{a.x + nx, a.y + ny, 0, 1, color, 0, 0},
                  {b.x + nx, b.y + ny, 0, 1, color, 0, 0},
                  {b.x - nx, b.y - ny, 0, 1, color, 0, 0},
                  {a.x - nx, a.y - ny, 0, 1, color, 0, 0}};
    for (unsigned i : {0u, 1u, 2u, 0u, 2u, 3u})
        vertices.push_back(v[i]);
    const auto clip = clips.back();
    if (!commands.empty() && !commands.back().texture &&
        std::memcmp(&commands.back().clip, &clip, sizeof(clip)) == 0)
        commands.back().count += 6;
    else
        commands.push_back({nullptr, clip, first, 6});
}
void DrawList::AddText(Font *font, float size, Point pos, Color color, const char *text, const char *end,
                       float wrap) {
    if (!font || !text || !std::isfinite(size) || size <= 0 || (end && end < text))
        return;
    std::string bounded;
    if (end) {
        bounded.assign(text, end);
        text = bounded.c_str();
    }
    LayoutText(*font, size, wrap, text, [&](const Glyph &glyph, float x, float y, float scale) {
        if (!glyph.texture)
            return;
        const Point a(pos.x + x + glyph.offset.x * scale, pos.y + y + glyph.offset.y * scale);
        AddImage(glyph.texture, a, {a.x + glyph.width * scale, a.y + glyph.height * scale}, glyph.uv0,
                 glyph.uv1, color);
    });
}

struct WindowState {
    std::string key, root;
    Point position, size, cursor;
    Rect clip;
    float scroll = 0, content = 0, maxY = 0;
    bool initialized = false, child = false;
    int flags = 0;
    float ScrollRange() const {
        return std::max(0.0f, std::floor(content - size.y));
    }
};
struct Context {
    Input io;
    HWND window = nullptr;
    IDirect3DDevice9 *device = nullptr;
    Font font;
    DrawList draw, foreground;
    std::map<std::string, WindowState> windows;
    std::vector<WindowState *> stack;
    std::vector<std::string> ids, order;
    std::vector<bool> disabled;
    std::string active, hoveredRoot;
    Rect item;
    Point nextPos, nextSize, pivot, previousMouse, pressMouse;
    bool hasPos = false, hasSize = false, firstOnly = false;
    bool pendingDown = false, pendingUp = false, pendingDouble = false, down = false, up = false,
         doubleClick = false;
    bool itemActive = false, itemHovered = false, itemActivated = false;
    bool inputEnabled = true, releasingCapture = false;
    float pendingWheel = 0, wheel = 0;
    ~Context() {
        if (device)
            device->Release();
    }
};
static Context *current = nullptr;
static WindowState &Window() {
    assert(current && !current->stack.empty());
    return *current->stack.back();
}
Context *CreateContext(HWND window, IDirect3DDevice9 *device) {
    auto state = std::make_unique<Context>();
    state->window = window;
    state->device = device;
    if (device)
        device->AddRef();
    return state.release();
}
void DestroyContext(Context *state) {
    if (!state)
        return;
    auto *previous = current;
    current = state;
    ClearInput();
    current = previous == state ? nullptr : previous;
    delete state;
}
void SetCurrentContext(Context *state) {
    current = state;
}
Input &GetIO() {
    assert(current);
    return current->io;
}
Font *LoadFont(const wchar_t *family, int pixels) {
    return current->font.Load(current->device, family, pixels) ? &current->font : nullptr;
}
Font *GetFont() {
    return &current->font;
}
void ClearInput() {
    if (!current)
        return;
    const bool release = current->io.MouseDown[0] && GetCapture() == current->window;
    current->active.clear();
    for (auto &down : current->io.MouseDown)
        down = false;
    current->pendingDown = current->pendingUp = current->pendingDouble = false;
    current->down = current->up = current->doubleClick = false;
    current->pendingWheel = current->wheel = 0;
    if (release) {
        current->releasingCapture = true;
        ReleaseCapture();
        current->releasingCapture = false;
    }
}
void SetInputEnabled(bool enabled) {
    if (!enabled)
        ClearInput();
    current->inputEnabled = enabled;
}
void Message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (!current)
        return;
    auto &state = *current;
    if (window != state.window)
        return;
    if (!state.inputEnabled)
        return;
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
        message == WM_LBUTTONDBLCLK)
        state.io.AddMousePosEvent(static_cast<float>(GET_X_LPARAM(lparam)),
                                  static_cast<float>(GET_Y_LPARAM(lparam)));
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) {
        state.pendingDown = true;
        state.io.MouseDown[0] = true;
        state.pressMouse = state.io.MousePos;
        if (window)
            SetCapture(window);
    }
    if (message == WM_LBUTTONDBLCLK)
        state.pendingDouble = true;
    if (message == WM_LBUTTONUP) {
        state.pendingUp = true;
        state.io.MouseDown[0] = false;
        if (window && GetCapture() == window) {
            state.releasingCapture = true;
            ReleaseCapture();
            state.releasingCapture = false;
        }
    }
    if (message == WM_MOUSEWHEEL)
        state.pendingWheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
    if (message == WM_KILLFOCUS || message == WM_CANCELMODE ||
        (message == WM_CAPTURECHANGED && !state.releasingCapture))
        ClearInput();
}
void NewFrame() {
    auto &state = *current;
    D3DVIEWPORT9 viewport{};
    if (state.device && SUCCEEDED(state.device->GetViewport(&viewport)))
        state.io.DisplaySize = {static_cast<float>(viewport.Width), static_cast<float>(viewport.Height)};
    state.io.MouseDelta = {state.io.MousePos.x - state.previousMouse.x,
                           state.io.MousePos.y - state.previousMouse.y};
    state.previousMouse = state.io.MousePos;
    state.down = state.pendingDown;
    state.up = state.pendingUp;
    state.doubleClick = state.pendingDouble;
    state.wheel = state.pendingWheel;
    state.pendingDown = state.pendingUp = state.pendingDouble = false;
    state.pendingWheel = 0;
    state.hoveredRoot.clear();
    for (auto it = state.order.rbegin(); it != state.order.rend(); ++it) {
        const auto &w = state.windows.at(*it);
        if (!(w.flags & NoInputs) &&
            Contains({w.position, {w.position.x + w.size.x, w.position.y + w.size.y}}, state.io.MousePos)) {
            state.hoveredRoot = *it;
            break;
        }
    }
    state.order.clear();
    state.stack.clear();
    state.ids.clear();
    state.disabled.clear();
    state.draw.Clear(state.io.DisplaySize);
    state.foreground.Clear(state.io.DisplaySize);
}
void SetNextWindowPos(Point pos, int condition, Point pivot) {
    current->nextPos = pos;
    current->pivot = pivot;
    current->hasPos = true;
    current->firstOnly = condition == FirstUse;
}
void SetNextWindowSize(Point size) {
    current->nextSize = size;
    current->hasSize = true;
}
bool Begin(const char *name, int flags) {
    auto &state = *current;
    const auto key = "w" + std::to_string(std::strlen(name)) + ":" + name;
    auto &window = state.windows[key];
    window.key = key;
    window.root = key;
    window.flags = flags;
    window.child = false;
    if (state.hasSize)
        window.size = state.nextSize;
    if (state.hasPos && (!state.firstOnly || !window.initialized))
        window.position = {state.nextPos.x - window.size.x * state.pivot.x,
                           state.nextPos.y - window.size.y * state.pivot.y};
    window.initialized = true;
    window.cursor = window.position;
    window.clip = Intersection(
        {window.position, {window.position.x + window.size.x, window.position.y + window.size.y}},
        {{0, 0}, state.io.DisplaySize});
    state.hasSize = state.hasPos = false;
    state.order.push_back(key);
    state.stack.push_back(&window);
    state.draw.PushClipRect(window.clip.min, window.clip.max);
    return true;
}
void End() {
    current->draw.PopClipRect();
    if (!current->stack.empty())
        current->stack.pop_back();
}
Point GetWindowPos() {
    return Window().position;
}
Point GetWindowSize() {
    return Window().size;
}
void SetWindowPos(Point pos) {
    auto &window = Window();
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y))
        return;
    if (window.position.x == pos.x && window.position.y == pos.y)
        return;
    window.cursor.x += pos.x - window.position.x;
    window.cursor.y += pos.y - window.position.y;
    window.position = pos;
    const auto parentClip = window.child && current->stack.size() > 1
                                ? current->stack[current->stack.size() - 2]->clip
                                : Rect{{0, 0}, current->io.DisplaySize};
    window.clip = Intersection({pos, {pos.x + window.size.x, pos.y + window.size.y}}, parentClip);
    current->draw.PopClipRect();
    current->draw.PushClipRect(window.clip.min, window.clip.max);
}
bool IsWindowHovered() {
    const auto &window = Window();
    return current->inputEnabled && !(window.flags & NoInputs) &&
           Contains(window.clip, current->io.MousePos) &&
           (current->hoveredRoot.empty() || current->hoveredRoot == window.root);
}
bool IsMouseHoveringRect(Point a, Point b) {
    return Contains({a, b}, current->io.MousePos);
}
DrawList *GetWindowDrawList() {
    return &current->draw;
}
DrawList *GetForegroundDrawList() {
    return &current->foreground;
}
Point GetCursorScreenPos() {
    return Window().cursor;
}
void SetCursorScreenPos(Point point) {
    Window().cursor = point;
}
Point GetItemRectMin() {
    return current->item.min;
}
Point GetItemRectMax() {
    return current->item.max;
}
void PushID(const char *text) {
    current->ids.push_back(text);
}
void PushID(int value) {
    PushID(std::to_string(value).c_str());
}
void PopID() {
    if (!current->ids.empty())
        current->ids.pop_back();
}
void BeginDisabled(bool value) {
    current->disabled.push_back(value || (!current->disabled.empty() && current->disabled.back()));
}
void EndDisabled() {
    if (!current->disabled.empty())
        current->disabled.pop_back();
}
bool InvisibleButton(const char *label, Point size) {
    auto &state = *current;
    auto &window = Window();
    const Point a = window.cursor, b(a.x + size.x, a.y + size.y);
    state.item = {a, b};
    std::string key = std::to_string(window.key.size()) + ":" + window.key;
    for (const auto &id : state.ids)
        key += std::to_string(id.size()) + ":" + id;
    key += std::to_string(std::strlen(label)) + ":" + label;
    const bool disabled = !state.disabled.empty() && state.disabled.back();
    state.itemHovered = !disabled && IsWindowHovered() && Contains({a, b}, state.io.MousePos);
    state.itemActivated = false;
    if (state.down && state.itemHovered && (state.active.empty() || state.active == key)) {
        state.active = key;
        state.itemActivated = true;
    }
    state.itemActive = !disabled && state.inputEnabled && state.active == key && state.io.MouseDown[0];
    const bool clicked = state.up && state.active == key && state.itemHovered;
    if (state.up && state.active == key)
        state.active.clear();
    window.cursor.y = b.y;
    window.maxY = std::max(window.maxY, b.y + window.scroll - window.position.y);
    return clicked;
}
bool IsItemActive() {
    return current->itemActive;
}
bool IsItemActivated() {
    return current->itemActivated;
}
bool IsItemHovered() {
    return current->itemHovered;
}
bool IsMouseDoubleClicked(int button) {
    return current->inputEnabled && !button && current->doubleClick;
}
bool IsMouseDragging(int button, float threshold) {
    const auto &io = current->io;
    return current->inputEnabled && button == 0 && io.MouseDown[0] &&
           (std::abs(io.MousePos.x - current->pressMouse.x) +
                std::abs(io.MousePos.y - current->pressMouse.y) >
            threshold);
}
void Dummy(Point size) {
    auto &w = Window();
    w.cursor.y += size.y;
    w.maxY = std::max(w.maxY, w.cursor.y + w.scroll - w.position.y);
}
bool BeginChild(const char *label, Point size, int flags) {
    auto &state = *current;
    auto &parent = Window();
    auto key = "c" + std::to_string(parent.key.size()) + ":" + parent.key;
    for (const auto &id : state.ids)
        key += std::to_string(id.size()) + ":" + id;
    key += std::to_string(std::strlen(label)) + ":" + label;
    auto &child = state.windows[key];
    child.key = key;
    child.root = parent.root;
    child.child = true;
    child.initialized = true;
    child.position = parent.cursor;
    child.size = size;
    child.flags = flags | (parent.flags & NoInputs);
    child.clip =
        Intersection({child.position, {child.position.x + size.x, child.position.y + size.y}}, parent.clip);
    const float maxScroll = child.ScrollRange();
    const bool disabled = !state.disabled.empty() && state.disabled.back();
    if (!disabled && IsWindowHovered() && Contains(child.clip, state.io.MousePos))
        child.scroll = std::clamp(child.scroll - state.wheel * 42.0f, 0.0f, maxScroll);
    child.scroll = std::clamp(child.scroll, 0.0f, maxScroll);
    child.maxY = 0;
    child.cursor = {child.position.x, child.position.y - child.scroll};
    state.stack.push_back(&child);
    state.draw.PushClipRect(child.clip.min, child.clip.max);
    return true;
}
void EndChild() {
    auto &state = *current;
    auto &child = Window();
    child.content = child.maxY;
    const float range = child.ScrollRange();
    if (range > 0 && child.size.x > 0 && child.size.y > 0) {
        const float width = std::max(7.0f, child.size.x / 76),
                    height =
                        std::min(child.size.y, std::max(24.0f, child.size.y * child.size.y / child.content));
        const Point top(child.position.x + child.size.x - width, child.position.y);
        const float travel = child.size.y - height;
        float y = top.y + travel * child.scroll / range;
        state.draw.AddRectFilled(top, {top.x + width, top.y + child.size.y}, RGBA(26, 32, 43, 255));
        child.cursor = {top.x, y};
        InvisibleButton("##scroll", {width, height});
        if (IsItemActive() && travel > 0) {
            child.scroll = std::clamp(child.scroll + state.io.MouseDelta.y * range / travel, 0.0f, range);
            y = top.y + travel * child.scroll / range;
        }
        state.draw.AddRectFilled({top.x, y}, {top.x + width, y + height},
                                 IsItemActive()    ? RGBA(96, 174, 250, 255)
                                 : IsItemHovered() ? RGBA(74, 130, 194, 255)
                                                   : RGBA(54, 76, 100, 255));
    }
    const auto bottom = Point(child.position.x, child.position.y + child.size.y);
    End();
    Window().cursor = bottom;
}

void Flush() {
    auto &state = *current;
    auto *device = state.device;
    if (!device || (state.draw.commands.empty() && state.foreground.commands.empty()))
        return;
    struct ClearDraw {
        Context &state;
        ~ClearDraw() {
            state.draw.Clear(state.io.DisplaySize);
            state.foreground.Clear(state.io.DisplaySize);
        }
    } clear{state};
    IDirect3DStateBlock9 *saved = nullptr;
    IDirect3DVertexBuffer9 *stream = nullptr;
    UINT offset = 0, stride = 0;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)))
        return;
    if (FAILED(saved->Capture())) {
        saved->Release();
        return;
    }
    device->GetStreamSource(0, &stream, &offset, &stride);
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    const D3DVIEWPORT9 viewport{
        0, 0, static_cast<DWORD>(state.io.DisplaySize.x), static_cast<DWORD>(state.io.DisplaySize.y), 0, 1};
    device->SetViewport(&viewport);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    device->SetRenderState(D3DRS_WRAP0, 0);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    for (auto *draw : {&state.draw, &state.foreground})
        for (const auto &command : draw->commands) {
            if (command.first > draw->vertices.size() ||
                command.count > draw->vertices.size() - command.first || command.count % 3)
                continue;
            RECT clip{static_cast<LONG>(std::max(0.0f, command.clip.min.x)),
                      static_cast<LONG>(std::max(0.0f, command.clip.min.y)),
                      static_cast<LONG>(std::min(state.io.DisplaySize.x, command.clip.max.x)),
                      static_cast<LONG>(std::min(state.io.DisplaySize.y, command.clip.max.y))};
            if (clip.left >= clip.right || clip.top >= clip.bottom)
                continue;
            device->SetScissorRect(&clip);
            device->SetTexture(0, command.texture);
            device->SetTextureStageState(0, D3DTSS_COLOROP,
                                         command.texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP,
                                         command.texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
            for (size_t at = 0; at < command.count;) {
                const UINT count = static_cast<UINT>(std::min<size_t>(command.count - at, 180000));
                device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, count / 3,
                                        draw->vertices.data() + command.first + at, sizeof(Vertex));
                at += count;
            }
        }
    saved->Apply();
    device->SetStreamSource(0, stream, offset, stride);
    if (stream)
        stream->Release();
    saved->Release();
}
void EndFrame() {
    auto &state = *current;
    if (state.up)
        state.active.clear();
}
void Render() {
    Flush();
    EndFrame();
}

}
