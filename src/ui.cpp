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
    unsigned atlasX = 0, atlasY = 0, rowHeight = 0;
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
    if (data->glyphs.size() >= 4095 && code != 0xfffd)
        return GetGlyph(0xfffd);
    Glyph glyph;
    if (!data->device) {
        glyph.advance = data->height * 0.5f;
        return data->glyphs.emplace(code, glyph).first->second;
    }
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
    constexpr unsigned atlasSize = 1024;
    const unsigned glyphWidth = static_cast<unsigned>(glyph.advance) + 4;
    const unsigned glyphHeight = static_cast<unsigned>(data->height) + 2;
    if (data->atlasX + glyphWidth > atlasSize) {
        data->atlasX = 0;
        data->atlasY += data->rowHeight;
        data->rowHeight = 0;
    }
    if (data->pages.empty() || data->atlasY + glyphHeight > atlasSize) {
        if (data->pages.size() >= 16)
            return data->glyphs.emplace(code, glyph).first->second;
        IDirect3DTexture9 *page = nullptr;
        if (FAILED(data->device->CreateTexture(atlasSize, atlasSize, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                               &page, nullptr)))
            return data->glyphs.emplace(code, glyph).first->second;
        D3DLOCKED_RECT lock{};
        if (FAILED(page->LockRect(0, &lock, nullptr, 0))) {
            page->Release();
            return data->glyphs.emplace(code, glyph).first->second;
        }
        for (unsigned y = 0; y < atlasSize; ++y)
            std::memset(static_cast<unsigned char *>(lock.pBits) + y * lock.Pitch, 0, atlasSize * 4);
        page->UnlockRect(0);
        const auto release = [](IDirect3DTexture9 *texture) { texture->Release(); };
        std::unique_ptr<IDirect3DTexture9, decltype(release)> owner(page, release);
        data->pages.push_back(page);
        owner.release();
        data->atlasX = data->atlasY = data->rowHeight = 0;
    }
    std::memset(data->pixels, 0, 128 * 128 * 4);
    RECT box{0, 0, 128, 128};
    ExtTextOutW(data->dc, 2, 0, ETO_CLIPPED | ETO_OPAQUE, &box, chars, length, nullptr);
    GdiFlush();
    const LONG x = static_cast<LONG>(data->atlasX), y = static_cast<LONG>(data->atlasY);
    RECT area{x, y, x + static_cast<LONG>(glyphWidth), y + static_cast<LONG>(glyphHeight)};
    D3DLOCKED_RECT lock{};
    auto *page = data->pages.back();
    if (FAILED(page->LockRect(0, &lock, &area, 0)))
        return data->glyphs.emplace(code, glyph).first->second;
    for (unsigned row = 0; row < glyphHeight; ++row) {
        auto *target =
            reinterpret_cast<uint32_t *>(static_cast<unsigned char *>(lock.pBits) + row * lock.Pitch);
        for (unsigned col = 0; col < glyphWidth; ++col) {
            const uint32_t pixel = data->pixels[row * 128 + col];
            const uint32_t alpha = std::max({pixel & 255, (pixel >> 8) & 255, (pixel >> 16) & 255});
            target[col] = (alpha << 24) | 0x00ffffff;
        }
    }
    page->UnlockRect(0);
    data->atlasX += glyphWidth;
    data->rowHeight = std::max(data->rowHeight, glyphHeight);
    glyph.texture = page;
    glyph.width = glyph.advance + 4;
    glyph.height = data->height;
    glyph.uv0 = {x / static_cast<float>(atlasSize), y / static_cast<float>(atlasSize)};
    glyph.uv1 = {(x + glyph.width) / static_cast<float>(atlasSize),
                 (y + glyph.height) / static_cast<float>(atlasSize)};
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
void DrawList::AddRectFilled(Point a, Point b, Color color, float rounding) {
    if (rounding <= 0 || !std::isfinite(rounding)) {
        AddImage(nullptr, a, b, {}, {1, 1}, color);
        return;
    }
    if (clips.empty() || !(color >> 24) || vertices.size() > 1000000 || !std::isfinite(a.x) ||
        !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y) || b.x <= a.x || b.y <= a.y)
        return;
    const auto clip = clips.back();
    if (b.x <= clip.min.x || b.y <= clip.min.y || a.x >= clip.max.x || a.y >= clip.max.y)
        return;
    const float radius = std::min(rounding, std::min(b.x - a.x, b.y - a.y) * 0.5f);
    const Point centers[] = {{b.x - radius, a.y + radius},
                             {b.x - radius, b.y - radius},
                             {a.x + radius, b.y - radius},
                             {a.x + radius, a.y + radius}};
    std::array<Point, 36> border{};
    std::size_t count = 0;
    for (int corner = 0; corner < 4; ++corner)
        for (int i = 0; i <= 8; ++i) {
            const float angle = (corner - 1 + i / 8.0f) * 1.570796327f;
            border[count++] = {centers[corner].x + std::cos(angle) * radius,
                               centers[corner].y + std::sin(angle) * radius};
        }
    const std::size_t first = vertices.size();
    const Point center{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    for (std::size_t i = 0; i < count; ++i)
        for (Point point : {center, border[i], border[(i + 1) % count]})
            vertices.push_back({point.x - 0.5f, point.y - 0.5f, 0, 1, color, 0, 0});
    if (!commands.empty() && !commands.back().texture &&
        std::memcmp(&commands.back().clip, &clip, sizeof(clip)) == 0)
        commands.back().count += vertices.size() - first;
    else
        commands.push_back({nullptr, clip, first, vertices.size() - first});
}
void DrawList::AddRect(Point a, Point b, Color color, float thickness, float rounding) {
    if (!std::isfinite(rounding) || rounding <= 0) {
        AddLine(a, {b.x, a.y}, color, thickness);
        AddLine({b.x, a.y}, b, color, thickness);
        AddLine(b, {a.x, b.y}, color, thickness);
        AddLine({a.x, b.y}, a, color, thickness);
        return;
    }
    if (b.x <= a.x || b.y <= a.y)
        return;
    const float radius = std::min(rounding, std::min(b.x - a.x, b.y - a.y) * 0.5f);
    const Point centers[] = {{b.x - radius, a.y + radius},
                             {b.x - radius, b.y - radius},
                             {a.x + radius, b.y - radius},
                             {a.x + radius, a.y + radius}};
    Point first{}, previous{};
    for (int corner = 0; corner < 4; ++corner)
        for (int i = 0; i <= 8; ++i) {
            const float angle = (corner - 1 + i / 8.0f) * 1.570796327f;
            Point point{centers[corner].x + std::cos(angle) * radius,
                        centers[corner].y + std::sin(angle) * radius};
            if (!corner && !i)
                first = point;
            else
                AddLine(previous, point, color, thickness);
            previous = point;
        }
    AddLine(previous, first, color, thickness);
}
void DrawList::AddCircleFilled(Point center, float radius, Color color) {
    AddRectFilled({center.x - radius, center.y - radius}, {center.x + radius, center.y + radius}, color,
                  radius);
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
    const auto bounds = clips.back();
    if (std::max(a.x, b.x) + std::abs(nx) <= bounds.min.x ||
        std::min(a.x, b.x) - std::abs(nx) >= bounds.max.x ||
        std::max(a.y, b.y) + std::abs(ny) <= bounds.min.y ||
        std::min(a.y, b.y) - std::abs(ny) >= bounds.max.y)
        return;
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
    bool initialized = false, child = false, popup = false;
    int flags = 0;
    float ScrollRange() const {
        return std::max(0.0f, std::floor(content - size.y));
    }
};
struct KeyEvent {
    unsigned key = 0;
    std::uint32_t character = 0;
    bool control = false, shift = false;
    std::string target;
};
struct FocusItem {
    std::string key;
    bool text = false;
    std::string root;
};
struct EditSnapshot {
    std::string value;
    std::size_t cursor = 0, anchor = 0;
};
struct EditState {
    std::string key, value;
    std::size_t cursor = 0, anchor = 0;
    float scroll = 0;
    std::vector<EditSnapshot> undo, redo;
};
struct TableState {
    Point origin;
    float width = 0, row = 0, bottom = 0;
    int columns = 0, column = -1, rowIndex = 0;
    bool clipping = false;
};
struct Context {
    Input io;
    Style style;
    std::vector<KeyEvent> pendingKeys, keys;
    std::vector<FocusItem> focusOrder;
    std::string focused, itemKey, popupId, scrollFocus, wheelTarget;
    std::string popupOwner, returnFocus;
    bool popupArrows = false;
    std::vector<std::string> scrollOrder;
    std::vector<TableState> tables;
    Point popupPosition;
    std::map<std::string, EditState> edits;
    ClipboardRead clipboardRead = nullptr;
    ClipboardWrite clipboardWrite = nullptr;
    void *clipboardUser = nullptr;
    unsigned highSurrogate = 0;
    bool focusNext = false, itemDisabled = false;
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
Style &GetStyle() {
    return current->style;
}
void SetTheme(Theme theme) {
    auto &style = current->style;
    const float size = style.FontSize, spacing = style.Spacing, rounding = style.Rounding;
    style = Style{};
    style.FontSize = size;
    style.Spacing = spacing;
    style.Rounding = rounding;
    if (theme == Theme::Dark) {
        style.Text = RGBA(230, 237, 243);
        style.Muted = RGBA(152, 166, 178);
        style.Background = RGBA(23, 30, 37);
        style.Field = RGBA(30, 40, 49);
        style.Button = RGBA(53, 71, 86);
        style.Hovered = RGBA(66, 89, 106);
        style.Active = RGBA(42, 59, 73);
        style.Accent = RGBA(64, 196, 119);
        style.Border = RGBA(54, 68, 79);
        style.Selection = RGBA(30, 65, 47);
    }
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
    current->focused.clear();
    current->returnFocus.clear();
    current->pendingKeys.clear();
    current->keys.clear();
    current->highSurrogate = 0;
    current->focusNext = false;
    current->io.WantCaptureKeyboard = current->io.WantTextInput = current->io.WantCaptureMouse = false;
    std::fill(std::begin(current->io.KeysDown), std::end(current->io.KeysDown), false);
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
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wparam < 256) {
        state.io.KeysDown[wparam] = true;
        if (state.pendingKeys.size() < 4096)
            state.pendingKeys.push_back({static_cast<unsigned>(wparam),
                                         0,
                                         state.io.KeysDown[VK_CONTROL],
                                         state.io.KeysDown[VK_SHIFT],
                                         {}});
    }
    if ((message == WM_KEYUP || message == WM_SYSKEYUP) && wparam < 256)
        state.io.KeysDown[wparam] = false;
    if (message == WM_CHAR || (message == WM_UNICHAR && wparam != UNICODE_NOCHAR)) {
        std::uint32_t code = static_cast<std::uint32_t>(wparam);
        if (message == WM_CHAR && code >= 0xd800 && code <= 0xdbff) {
            state.highSurrogate = code;
        } else {
            if (message == WM_CHAR && code >= 0xdc00 && code <= 0xdfff) {
                code = state.highSurrogate ? 0x10000 + ((state.highSurrogate - 0xd800) << 10) + code - 0xdc00
                                           : 0xfffd;
            } else if (state.highSurrogate && state.pendingKeys.size() < 4096)
                state.pendingKeys.push_back({0, 0xfffd, false, false, {}});
            state.highSurrogate = 0;
            if (code >= 32 && code != 127 && state.pendingKeys.size() < 4096)
                state.pendingKeys.push_back({0, code, false, false, {}});
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
    state.wheelTarget.clear();
    if (state.inputEnabled && state.wheel)
        for (auto it = state.scrollOrder.rbegin(); it != state.scrollOrder.rend(); ++it) {
            const auto &child = state.windows.at(*it);
            if ((child.popup || child.root == state.hoveredRoot) && (state.popupId.empty() || child.popup) &&
                Contains(child.clip, state.io.MousePos) &&
                ((state.wheel > 0 && child.scroll > 0) ||
                 (state.wheel < 0 && child.scroll < child.ScrollRange()))) {
                state.wheelTarget = *it;
                break;
            }
        }
    state.scrollOrder.clear();
    state.scrollFocus.clear();
    state.keys = std::move(state.pendingKeys);
    state.pendingKeys.clear();
    if (!state.returnFocus.empty()) {
        state.focused = std::move(state.returnFocus);
        state.returnFocus.clear();
        state.scrollFocus = state.focused;
    }
    if (!state.popupId.empty()) {
        const auto root = "w" + std::to_string(state.popupId.size()) + ":" + state.popupId;
        state.focusOrder.erase(std::remove_if(state.focusOrder.begin(), state.focusOrder.end(),
                                              [&](const FocusItem &item) { return item.root != root; }),
                               state.focusOrder.end());
    }
    if (state.down)
        state.focused.clear();
    for (auto &event : state.keys) {
        if (event.key == VK_ESCAPE && !state.popupId.empty()) {
            CloseCurrentPopup();
            state.focusOrder.clear();
        }
        const bool popupNavigation =
            state.popupArrows && !state.popupId.empty() &&
            (event.key == VK_UP || event.key == VK_DOWN || event.key == VK_HOME || event.key == VK_END);
        if ((event.key == VK_TAB || popupNavigation) && !state.focusOrder.empty()) {
            auto found = std::find_if(state.focusOrder.begin(), state.focusOrder.end(),
                                      [&](const FocusItem &item) { return item.key == state.focused; });
            const auto count = static_cast<int>(state.focusOrder.size());
            const bool backward = event.key == VK_UP || (event.key == VK_TAB && event.shift);
            int index = found == state.focusOrder.end() ? (backward ? 0 : -1)
                                                        : static_cast<int>(found - state.focusOrder.begin());
            index = event.key == VK_HOME  ? 0
                    : event.key == VK_END ? count - 1
                                          : (index + (backward ? count - 1 : 1)) % count;
            state.focused = state.focusOrder[index].key;
            state.scrollFocus = state.focused;
        }
        event.target = state.focused;
    }
    state.focusOrder.clear();
    state.io.WantTextInput = false;
    state.io.WantCaptureKeyboard = !state.focused.empty();
    state.io.WantCaptureMouse = !state.hoveredRoot.empty() || !state.active.empty() || !state.popupId.empty();
    if (state.down && !state.popupId.empty()) {
        const auto key = "w" + std::to_string(state.popupId.size()) + ":" + state.popupId;
        const auto popup = state.windows.find(key);
        if (popup != state.windows.end() && !Contains(popup->second.clip, state.io.MousePos)) {
            CloseCurrentPopup();
            state.down = false;
            state.active.clear();
        }
    }
    state.order.clear();
    state.stack.clear();
    state.ids.clear();
    state.disabled.clear();
    state.tables.clear();
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
    window.popup = !state.popupId.empty() && name == state.popupId;
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
    GetWindowDrawList()->PushClipRect(window.clip.min, window.clip.max);
    return true;
}
void End() {
    if (!current->stack.empty())
        GetWindowDrawList()->PopClipRect();
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
    GetWindowDrawList()->PopClipRect();
    GetWindowDrawList()->PushClipRect(window.clip.min, window.clip.max);
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
    return !current->stack.empty() && Window().popup ? &current->foreground : &current->draw;
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
bool InvisibleButton(const char *label, Point size, bool keyboard_focus) {
    auto &state = *current;
    auto &window = Window();
    const Point a = window.cursor, b(a.x + size.x, a.y + size.y);
    state.item = {a, b};
    std::string key = std::to_string(window.key.size()) + ":" + window.key;
    for (const auto &id : state.ids)
        key += std::to_string(id.size()) + ":" + id;
    key += std::to_string(std::strlen(label)) + ":" + label;
    state.itemKey = key;
    const bool disabled = (!state.disabled.empty() && state.disabled.back()) || !state.inputEnabled ||
                          (window.flags & NoInputs) || (!state.popupId.empty() && !window.popup);
    state.itemDisabled = disabled;
    if (keyboard_focus && !disabled && std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 &&
        size.y > 0) {
        state.focusOrder.push_back({key, false, window.root});
        if (state.focusNext) {
            state.focused = key;
            state.scrollFocus = key;
            state.focusNext = false;
        }
    }
    state.itemHovered = !disabled && IsWindowHovered() && Contains({a, b}, state.io.MousePos);
    state.itemActivated = false;
    if (state.down && state.itemHovered && (state.active.empty() || state.active == key)) {
        state.active = key;
        if (keyboard_focus)
            state.focused = key;
        state.itemActivated = true;
    }
    state.itemActive = !disabled && state.inputEnabled && state.active == key && state.io.MouseDown[0];
    bool clicked = state.up && state.active == key && state.itemHovered;
    if (!disabled && keyboard_focus) {
        for (const auto &event : state.keys)
            if ((event.target == key || (event.target.empty() && state.focused == key)) &&
                (event.key == VK_RETURN || event.key == VK_SPACE))
                clicked = true;
    }
    if (!disabled && state.focused == key) {
        state.io.WantCaptureKeyboard = true;
        if (window.child && state.scrollFocus == key) {
            if (a.y < window.clip.min.y)
                window.scroll = std::max(0.0f, window.scroll - (window.clip.min.y - a.y));
            else if (b.y > window.clip.max.y)
                window.scroll = std::min(window.ScrollRange(), window.scroll + b.y - window.clip.max.y);
        }
    }
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
bool IsItemDisabled() {
    return current->itemDisabled;
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
    current->item = {w.cursor, {w.cursor.x + size.x, w.cursor.y + size.y}};
    w.cursor.y += size.y;
    w.maxY = std::max(w.maxY, w.cursor.y + w.scroll - w.position.y);
}
float GetScrollY() {
    return Window().scroll;
}
void SetScrollY(float value) {
    auto &window = Window();
    if (!window.child || !std::isfinite(value))
        return;
    value = std::clamp(value, 0.0f, window.ScrollRange());
    window.cursor.y += window.scroll - value;
    window.scroll = value;
}
void SetContentHeight(float height) {
    auto &window = Window();
    if (!window.child || !std::isfinite(height) || height < 0 || height > 16777216)
        return;
    window.content = height;
    window.maxY = height;
    SetScrollY(window.scroll);
}
RowRange VisibleRows(int count, float height) {
    const auto &window = Window();
    if (count <= 0 || count > 1000000 || !std::isfinite(height) || height < 1 ||
        static_cast<double>(count) * height > 16777216 || window.clip.max.y <= window.clip.min.y)
        return {};
    const double first = std::floor((static_cast<double>(window.clip.min.y) - window.cursor.y) / height);
    const double last = std::ceil((static_cast<double>(window.clip.max.y) - window.cursor.y) / height);
    return {static_cast<int>(std::clamp(first, 0.0, static_cast<double>(count))),
            static_cast<int>(std::clamp(last, 0.0, static_cast<double>(count)))};
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
    child.popup = parent.popup;
    child.initialized = true;
    child.position = parent.cursor;
    child.size = size;
    child.flags = flags | (parent.flags & NoInputs);
    child.clip =
        Intersection({child.position, {child.position.x + size.x, child.position.y + size.y}}, parent.clip);
    const float maxScroll = child.ScrollRange();
    const bool disabled =
        (!state.disabled.empty() && state.disabled.back()) || !state.inputEnabled || (child.flags & NoInputs);
    if (!disabled)
        state.scrollOrder.push_back(key);
    if (!disabled && state.wheelTarget == key && IsWindowHovered() && Contains(child.clip, state.io.MousePos))
        child.scroll = std::clamp(child.scroll - state.wheel * 42.0f, 0.0f, maxScroll);
    child.scroll = std::clamp(child.scroll, 0.0f, maxScroll);
    child.maxY = 0;
    child.cursor = {child.position.x, child.position.y - child.scroll};
    state.stack.push_back(&child);
    GetWindowDrawList()->PushClipRect(child.clip.min, child.clip.max);
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
        GetWindowDrawList()->AddRectFilled(top, {top.x + width, top.y + child.size.y}, state.style.Field);
        child.cursor = {top.x, y};
        InvisibleButton("##scroll", {width, height}, false);
        if (IsItemActive() && travel > 0) {
            child.scroll = std::clamp(child.scroll + state.io.MouseDelta.y * range / travel, 0.0f, range);
            y = top.y + travel * child.scroll / range;
        }
        GetWindowDrawList()->AddRectFilled({top.x, y}, {top.x + width, y + height},
                                           IsItemActive()    ? state.style.Accent
                                           : IsItemHovered() ? state.style.Muted
                                                             : state.style.Border);
    }
    const auto bottom = Point(child.position.x, child.position.y + child.size.y);
    End();
    Window().cursor = bottom;
    Dummy({0, 0});
}

bool IsItemFocused() {
    return !current->itemDisabled && current->focused == current->itemKey;
}
void SetKeyboardFocusHere() {
    current->focusNext = true;
}
bool IsKeyPressed(unsigned key) {
    return current->inputEnabled && !current->itemDisabled &&
           std::any_of(current->keys.begin(), current->keys.end(), [&](const KeyEvent &event) {
               return event.key == key && (event.target == current->itemKey ||
                                           (event.target.empty() && current->focused == current->itemKey));
           });
}
void SetClipboardHandlers(ClipboardRead read, ClipboardWrite write, void *user) {
    current->clipboardRead = read;
    current->clipboardWrite = write;
    current->clipboardUser = user;
}
static std::string Encode(std::uint32_t code) {
    if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
        code = 0xfffd;
    std::string result;
    if (code < 0x80)
        result += static_cast<char>(code);
    else {
        if (code < 0x800)
            result += static_cast<char>(0xc0 | (code >> 6));
        else {
            if (code < 0x10000)
                result += static_cast<char>(0xe0 | (code >> 12));
            else {
                result += static_cast<char>(0xf0 | (code >> 18));
                result += static_cast<char>(0x80 | ((code >> 12) & 63));
            }
            result += static_cast<char>(0x80 | ((code >> 6) & 63));
        }
        result += static_cast<char>(0x80 | (code & 63));
    }
    return result;
}
static std::string CleanText(const std::string &value, std::size_t limit) {
    std::string result;
    const char *at = value.c_str();
    while (*at && result.size() < limit) {
        const auto code = NextCodepoint(at);
        if (code < 32 || code == 127)
            continue;
        const auto encoded = Encode(code);
        if (encoded.size() > limit - result.size())
            break;
        result += encoded;
    }
    return result;
}
static std::size_t Previous(const std::string &value, std::size_t at) {
    if (at)
        --at;
    while (at && (static_cast<unsigned char>(value[at]) & 0xc0) == 0x80)
        --at;
    return at;
}
static std::size_t Next(const std::string &value, std::size_t at) {
    if (at >= value.size())
        return value.size();
    const char *start = value.c_str(), *point = start + at;
    NextCodepoint(point);
    return static_cast<std::size_t>(point - start);
}
static bool ReadClipboard(std::string &value) {
    auto &state = *current;
    if (state.clipboardRead)
        return state.clipboardRead(value, state.clipboardUser);
    if (!state.window || !OpenClipboard(state.window))
        return false;
    bool success = false;
    const auto handle = GetClipboardData(CF_UNICODETEXT);
    const auto size = handle ? GlobalSize(handle) / sizeof(wchar_t) : 0;
    if (size > 0 && size <= 1048576) {
        const auto *text = static_cast<const wchar_t *>(GlobalLock(handle));
        if (text) {
            std::size_t length = 0;
            while (length < size && text[length])
                ++length;
            if (length < size) {
                const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0,
                                                      nullptr, nullptr);
                value.resize(bytes);
                success = !length || WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length),
                                                         value.data(), bytes, nullptr, nullptr) == bytes;
            }
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return success;
}
static bool WriteClipboard(const std::string &value) {
    auto &state = *current;
    if (state.clipboardWrite)
        return state.clipboardWrite(value, state.clipboardUser);
    if (!state.window || value.size() > 1048576)
        return false;
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
    if (length <= 0)
        return false;
    auto memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(length) * sizeof(wchar_t));
    if (!memory)
        return false;
    auto *text = static_cast<wchar_t *>(GlobalLock(memory));
    if (!text) {
        GlobalFree(memory);
        return false;
    }
    const bool converted =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, text, length) == length;
    GlobalUnlock(memory);
    bool success = false;
    if (converted && OpenClipboard(state.window)) {
        success = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
        CloseClipboard();
    }
    if (!success)
        GlobalFree(memory);
    return success;
}
static void Remember(std::vector<EditSnapshot> &history, const EditState &edit, const std::string &value) {
    std::size_t bytes = value.size();
    for (const auto &snapshot : history)
        bytes += snapshot.value.size();
    while (!history.empty() && (history.size() >= 64 || bytes > 1048576)) {
        bytes -= history.front().value.size();
        history.erase(history.begin());
    }
    history.push_back({value, edit.cursor, edit.anchor});
}
bool InputText(const char *id, std::string &value, float width, std::size_t max_bytes, int flags) {
    if (!id || !std::isfinite(width) || width < 40 || value.size() > 1048576 || max_bytes > 1048576)
        return false;
    auto &state = *current;
    auto &style = state.style;
    const auto position = GetCursorScreenPos();
    const float labelHeight = style.FontSize + 6, height = style.FontSize + 16;
    InvisibleButton(id, {width, labelHeight + height});
    const bool focused = IsItemFocused();
    const bool readOnly = (flags & TextReadOnly) != 0, password = (flags & TextPassword) != 0;
    bool changed = false, submitted = false;
    auto &edit = state.edits[state.itemKey];
    const bool receives =
        !state.itemDisabled &&
        (focused || std::any_of(state.keys.begin(), state.keys.end(), [&](const KeyEvent &event) {
             return !event.target.empty() && event.target == state.itemKey;
         }));
    if (receives) {
        if (edit.key != state.itemKey || edit.value != value) {
            edit = {};
            edit.key = state.itemKey;
            edit.cursor = edit.anchor = value.size();
            edit.value = value;
        }
        if (!state.focusOrder.empty())
            state.focusOrder.back().text = true;
        if (focused)
            state.io.WantTextInput = !readOnly;
    }
    const auto display = [&](std::size_t end) {
        if (!password)
            return value.substr(0, end);
        std::string masked;
        for (std::size_t at = 0; at < end; at = Next(value, at))
            masked += '*';
        return masked;
    };
    const auto measure = [&](std::size_t end) {
        return state.font.MeasureText(display(end).c_str(), style.FontSize).x;
    };
    const auto mouseCursor = [&] {
        const float x = state.io.MousePos.x - position.x - 8 + edit.scroll;
        float previous = 0;
        for (std::size_t at = 0; at < value.size();) {
            const auto next = Next(value, at);
            const auto character = password ? std::string("*") : value.substr(at, next - at);
            const float nextX = previous + state.font.MeasureText(character.c_str(), style.FontSize).x;
            if (x < (previous + nextX) * 0.5f)
                return at;
            previous = nextX;
            at = next;
        }
        return value.size();
    };
    if (focused && IsItemActivated()) {
        edit.cursor = mouseCursor();
        if (!state.io.KeysDown[VK_SHIFT])
            edit.anchor = edit.cursor;
    }
    if (focused && IsItemActive())
        edit.cursor = mouseCursor();
    if (focused && IsItemHovered() && IsMouseDoubleClicked(0)) {
        edit.anchor = 0;
        edit.cursor = value.size();
    }
    const auto replace = [&](const std::string &insertion) {
        if (readOnly)
            return;
        const auto lo = std::min(edit.cursor, edit.anchor), hi = std::max(edit.cursor, edit.anchor);
        const auto kept = value.size() - (hi - lo);
        const auto clean = CleanText(insertion, max_bytes > kept ? max_bytes - kept : 0);
        if (lo == hi && clean.empty())
            return;
        Remember(edit.undo, edit, value);
        edit.redo.clear();
        value.replace(lo, hi - lo, clean);
        edit.cursor = edit.anchor = lo + clean.size();
        changed = true;
    };
    if (receives)
        for (const auto &event : state.keys) {
            if (!event.target.empty() && event.target != state.itemKey)
                continue;
            if (event.character) {
                replace(Encode(event.character));
                continue;
            }
            if (event.key == VK_RETURN)
                submitted = true;
            if (event.control && event.key == 'A') {
                edit.anchor = 0;
                edit.cursor = value.size();
            } else if (event.control && (event.key == 'C' || event.key == 'X')) {
                const auto lo = std::min(edit.cursor, edit.anchor), hi = std::max(edit.cursor, edit.anchor);
                if (!password && lo != hi && WriteClipboard(value.substr(lo, hi - lo)) && event.key == 'X')
                    replace({});
            } else if (event.control && event.key == 'V' && !readOnly) {
                std::string pasted;
                if (ReadClipboard(pasted) && pasted.size() <= 1048576)
                    replace(pasted);
            } else if (event.control && (event.key == 'Z' || event.key == 'Y') && !readOnly) {
                auto &from = event.key == 'Y' || event.shift ? edit.redo : edit.undo;
                auto &to = &from == &edit.redo ? edit.undo : edit.redo;
                if (!from.empty()) {
                    Remember(to, edit, value);
                    auto saved = std::move(from.back());
                    from.pop_back();
                    value = std::move(saved.value);
                    edit.cursor = saved.cursor;
                    edit.anchor = saved.anchor;
                    changed = true;
                }
            } else if ((event.key == VK_BACK || event.key == VK_DELETE) && !readOnly) {
                if (edit.cursor == edit.anchor) {
                    if (event.key == VK_BACK)
                        edit.anchor = Previous(value, edit.cursor);
                    else
                        edit.anchor = Next(value, edit.cursor);
                }
                replace({});
            } else if (event.key == VK_LEFT || event.key == VK_RIGHT || event.key == VK_HOME ||
                       event.key == VK_END) {
                if (event.key == VK_HOME)
                    edit.cursor = 0;
                else if (event.key == VK_END)
                    edit.cursor = value.size();
                else if (!event.shift && edit.cursor != edit.anchor)
                    edit.cursor = event.key == VK_LEFT ? std::min(edit.cursor, edit.anchor)
                                                       : std::max(edit.cursor, edit.anchor);
                else {
                    edit.cursor =
                        event.key == VK_LEFT ? Previous(value, edit.cursor) : Next(value, edit.cursor);
                    if (event.control)
                        while (edit.cursor > 0 && edit.cursor < value.size() && value[edit.cursor] != ' ')
                            edit.cursor = event.key == VK_LEFT ? Previous(value, edit.cursor)
                                                               : Next(value, edit.cursor);
                }
                if (!event.shift)
                    edit.anchor = edit.cursor;
            }
        }
    if (receives) {
        edit.value = value;
        const float caret = measure(edit.cursor), available = width - 16;
        edit.scroll = std::clamp(edit.scroll, std::max(0.0f, caret - available), caret);
    }
    auto *draw = GetWindowDrawList();
    const Point top{position.x, position.y + labelHeight}, bottom{position.x + width, top.y + height};
    draw->AddText(GetFont(), style.FontSize, position, style.Text, id);
    draw->AddRectFilled(top, bottom, style.Field, style.Rounding);
    draw->AddRect(top, bottom, focused ? style.Accent : style.Border, 1, style.Rounding);
    draw->PushClipRect({top.x + 7, top.y}, {bottom.x - 7, bottom.y});
    const float scroll = focused ? edit.scroll : 0;
    if (focused && edit.cursor != edit.anchor) {
        const float first = measure(std::min(edit.cursor, edit.anchor)),
                    last = measure(std::max(edit.cursor, edit.anchor));
        draw->AddRectFilled({top.x + 8 + first - scroll, top.y + 5},
                            {top.x + 8 + last - scroll, bottom.y - 5}, style.Selection);
    }
    const auto text = display(value.size());
    draw->AddText(GetFont(), style.FontSize, {top.x + 8 - scroll, top.y + 8},
                  state.itemDisabled ? style.Muted : style.Text, text.c_str());
    if (focused && (GetTickCount64() % 1000 < 650 || changed)) {
        const float caret = top.x + 8 + measure(edit.cursor) - scroll;
        draw->AddLine({caret, top.y + 5}, {caret, bottom.y - 5}, style.Accent);
    }
    draw->PopClipRect();
    return flags & TextEnterReturnsTrue ? submitted : changed;
}
void SameLine(float spacing) {
    auto &window = Window();
    const auto item = current->item;
    window.cursor = {item.max.x + (spacing < 0 ? current->style.Spacing : spacing), item.min.y};
}
void Spacing(float height) {
    Dummy({0, height < 0 ? current->style.Spacing : height});
}
void Separator(float width) {
    const auto position = GetCursorScreenPos();
    if (width <= 0)
        width = Window().position.x + Window().size.x - position.x;
    GetWindowDrawList()->AddLine(position, {position.x + width, position.y}, current->style.Border);
    Dummy({0, current->style.Spacing});
}
bool BeginTable(const char *id, int columns, float width) {
    if (!id || columns < 1 || columns > 64 || !std::isfinite(width))
        return false;
    const auto origin = GetCursorScreenPos();
    if (width <= 0)
        width = Window().position.x + Window().size.x - origin.x;
    if (width <= 0)
        return false;
    PushID(id);
    current->tables.push_back({origin, width, origin.y, origin.y, columns, -1, 0, false});
    return true;
}
void TableNextColumn() {
    assert(!current->tables.empty());
    auto &table = current->tables.back();
    if (table.clipping) {
        table.bottom = std::max(table.bottom, GetCursorScreenPos().y);
        GetWindowDrawList()->PopClipRect();
        PopID();
    }
    if (++table.column == table.columns) {
        table.column = 0;
        ++table.rowIndex;
        table.row = table.bottom + current->style.Spacing;
    }
    const float width = table.width / table.columns;
    const float left = table.origin.x + width * table.column;
    SetCursorScreenPos({left, table.row});
    GetWindowDrawList()->PushClipRect({left, table.row},
                                      {left + width - current->style.Spacing, Window().clip.max.y});
    PushID(table.rowIndex * table.columns + table.column);
    table.clipping = true;
}
void EndTable() {
    assert(!current->tables.empty());
    auto table = current->tables.back();
    current->tables.pop_back();
    if (table.clipping) {
        table.bottom = std::max(table.bottom, GetCursorScreenPos().y);
        GetWindowDrawList()->PopClipRect();
        PopID();
    }
    PopID();
    SetCursorScreenPos({table.origin.x, table.bottom});
    Dummy({0, 0});
}
static std::string PopupName(const char *id) {
    std::string key = "popup:" + Window().key;
    for (const auto &part : current->ids)
        key += std::to_string(part.size()) + ":" + part;
    return key + ":" + id;
}
void OpenPopup(const char *id) {
    if (!id)
        return;
    current->popupId = PopupName(id);
    current->popupArrows = false;
    current->popupOwner = current->itemKey;
    current->popupPosition = {current->item.min.x, current->item.max.y + 4};
    current->focused.clear();
}
bool BeginPopup(const char *id, Point size, bool directional_navigation) {
    const auto name = PopupName(id);
    if (current->popupId != name)
        return false;
    current->popupArrows = directional_navigation;
    const auto screen = GetIO().DisplaySize;
    size.x = std::min(size.x, screen.x);
    size.y = std::min(size.y, screen.y);
    const Point position{std::clamp(current->popupPosition.x, 0.0f, std::max(0.0f, screen.x - size.x)),
                         std::clamp(current->popupPosition.y, 0.0f, std::max(0.0f, screen.y - size.y))};
    SetNextWindowPos(position);
    SetNextWindowSize(size);
    Begin(name.c_str());
    GetWindowDrawList()->AddRectFilled(position, {position.x + size.x, position.y + size.y},
                                       current->style.Background, current->style.Rounding);
    GetWindowDrawList()->AddRect(position, {position.x + size.x, position.y + size.y}, current->style.Border,
                                 1, current->style.Rounding);
    current->hoveredRoot = Window().key;
    return true;
}
void EndPopup() {
    End();
}
void CloseCurrentPopup() {
    current->popupId.clear();
    current->focused.clear();
    current->returnFocus = current->popupOwner;
    current->popupOwner.clear();
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
    if (std::none_of(state.focusOrder.begin(), state.focusOrder.end(),
                     [&](const FocusItem &item) { return item.key == state.focused; })) {
        state.focused.clear();
        state.io.WantCaptureKeyboard = state.io.WantTextInput = false;
    }
    for (auto it = state.edits.begin(); it != state.edits.end();) {
        if (std::none_of(state.focusOrder.begin(), state.focusOrder.end(),
                         [&](const FocusItem &item) { return item.key == it->first; }))
            it = state.edits.erase(it);
        else
            ++it;
    }
    state.keys.clear();
}
void Render() {
    Flush();
    EndFrame();
}

}
