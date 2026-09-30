#include <aardvark/ui.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace aardvark::ui {

static void FocusOutline(Point top, Point size) {
    if (IsItemFocused())
        GetWindowDrawList()->AddRect({top.x - 2, top.y - 2}, {top.x + size.x + 2, top.y + size.y + 2},
                                     GetStyle().Accent, 1, GetStyle().Rounding);
}

void Text(const char *text, float size, Color color, float wrap) {
    if (!text)
        return;
    auto *font = GetFont();
    GetWindowDrawList()->AddText(font, size, GetCursorScreenPos(), color ? color : GetStyle().Text, text,
                                 nullptr, wrap);
    Dummy(font->MeasureText(text, size, wrap));
}

bool Button(const char *label, Point size) {
    const auto position = GetCursorScreenPos();
    const bool clicked = InvisibleButton(label, size);
    const auto &style = GetStyle();
    auto *draw = GetWindowDrawList();
    const auto color = IsItemDisabled()  ? style.Field
                       : IsItemActive()  ? style.Active
                       : IsItemHovered() ? style.Hovered
                                         : style.Button;
    draw->AddRectFilled(position, {position.x + size.x, position.y + size.y}, color, style.Rounding);
    FocusOutline(position, size);
    const auto text = GetFont()->MeasureText(label, style.FontSize);
    draw->PushClipRect({position.x + 6, position.y}, {position.x + size.x - 6, position.y + size.y});
    draw->AddText(GetFont(), style.FontSize,
                  {position.x + (size.x - text.x) / 2, position.y + (size.y - text.y) / 2},
                  IsItemDisabled() ? style.Muted : White, label);
    draw->PopClipRect();
    return clicked;
}

bool Checkbox(const char *label, bool &value) {
    const auto position = GetCursorScreenPos();
    const auto &style = GetStyle();
    const auto text = GetFont()->MeasureText(label, style.FontSize);
    const bool clicked = InvisibleButton(label, {30 + text.x, 28});
    if (clicked)
        value = !value;
    auto *draw = GetWindowDrawList();
    const Point end{position.x + 20, position.y + 20};
    draw->AddRectFilled(position, end, value ? style.Accent : style.Field, 4);
    draw->AddRect(position, end, IsItemFocused() || IsItemHovered() ? style.Accent : style.Border, 1, 4);
    if (value) {
        draw->AddLine({position.x + 4, position.y + 10}, {position.x + 8, position.y + 14}, White, 2);
        draw->AddLine({position.x + 8, position.y + 14}, {position.x + 16, position.y + 6}, White, 2);
    }
    draw->AddText(GetFont(), style.FontSize, {position.x + 30, position.y + 1}, style.Text, label);
    return clicked;
}

bool SliderFloat(const char *label, float &value, float min, float max, float width) {
    if (!std::isfinite(min) || !std::isfinite(max) || max <= min || !std::isfinite(max - min) ||
        !std::isfinite(width) || width < 40)
        return false;
    const auto position = GetCursorScreenPos();
    const auto &style = GetStyle();
    const auto before = value;
    if (!std::isfinite(value))
        value = min;
    value = std::clamp(value, min, max);
    InvisibleButton(label, {width, 52});
    if (IsItemActive() || IsItemActivated()) {
        const auto fraction = std::clamp((GetIO().MousePos.x - position.x) / width, 0.0f, 1.0f);
        value = min + fraction * (max - min);
    }
    {
        if (IsKeyPressed(VK_LEFT) || IsKeyPressed(VK_DOWN))
            value -= (max - min) / 100;
        if (IsKeyPressed(VK_RIGHT) || IsKeyPressed(VK_UP))
            value += (max - min) / 100;
        if (IsKeyPressed(VK_HOME))
            value = min;
        if (IsKeyPressed(VK_END))
            value = max;
        value = std::clamp(value, min, max);
    }
    auto *draw = GetWindowDrawList();
    draw->PushClipRect(position, {position.x + width, position.y + 52});
    draw->AddText(GetFont(), style.FontSize, position, style.Text, label);
    char number[32]{};
    std::snprintf(number, sizeof(number), "%.2f", value);
    const auto text = GetFont()->MeasureText(number, style.FontSize);
    draw->AddText(GetFont(), style.FontSize, {position.x + width - text.x, position.y}, style.Muted, number);
    const float fraction = (value - min) / (max - min);
    draw->AddRectFilled({position.x, position.y + 33}, {position.x + width, position.y + 39}, style.Border,
                        3);
    draw->AddRectFilled({position.x, position.y + 33}, {position.x + width * fraction, position.y + 39},
                        style.Accent, 3);
    const float knob = position.x + std::clamp(width * fraction, 7.0f, width - 7);
    draw->AddCircleFilled({knob, position.y + 36}, IsItemFocused() ? 8.0f : 7.0f, style.Accent);
    draw->PopClipRect();
    return before != value;
}

bool Selectable(const char *label, bool selected, Point size) {
    const auto position = GetCursorScreenPos();
    const bool clicked = InvisibleButton(label, size);
    const auto &style = GetStyle();
    auto *draw = GetWindowDrawList();
    if (selected || IsItemHovered() || IsItemFocused())
        draw->AddRectFilled(position, {position.x + size.x, position.y + size.y},
                            selected ? style.Selection : style.Field, style.Rounding);
    draw->PushClipRect(position, {position.x + size.x, position.y + size.y});
    draw->AddText(GetFont(), style.FontSize, {position.x + 10, position.y + (size.y - style.FontSize) / 2},
                  selected ? style.Accent : style.Text, label);
    draw->PopClipRect();
    return clicked;
}

bool RadioButton(const char *label, int &value, int option) {
    const auto position = GetCursorScreenPos();
    const auto &style = GetStyle();
    const auto extent = GetFont()->MeasureText(label, style.FontSize);
    const bool clicked = InvisibleButton(label, {extent.x + 30, 28});
    if (clicked)
        value = option;
    auto *draw = GetWindowDrawList();
    draw->AddCircleFilled({position.x + 10, position.y + 10}, 10,
                          IsItemFocused() || value == option ? style.Accent : style.Border);
    draw->AddCircleFilled({position.x + 10, position.y + 10}, 8, style.Background);
    if (value == option)
        draw->AddCircleFilled({position.x + 10, position.y + 10}, 5, style.Accent);
    draw->AddText(GetFont(), style.FontSize, {position.x + 30, position.y + 1}, style.Text, label);
    return clicked;
}

bool CollapsingHeader(const char *label, bool &open, float width) {
    const auto position = GetCursorScreenPos();
    if (InvisibleButton(label, {width, 34}))
        open = !open;
    const auto &style = GetStyle();
    auto *draw = GetWindowDrawList();
    draw->AddRectFilled(position, {position.x + width, position.y + 34}, style.Field, style.Rounding);
    FocusOutline(position, {width, 34});
    draw->PushClipRect(position, {position.x + width, position.y + 34});
    draw->AddText(GetFont(), style.FontSize, {position.x + 10, position.y + 8}, style.Muted,
                  open ? "-" : "+");
    draw->AddText(GetFont(), style.FontSize, {position.x + 32, position.y + 8}, style.Text, label);
    draw->PopClipRect();
    return open;
}

bool Combo(const char *label, int &selected, const char *const *items, int count, float width) {
    if (!label || !items || count <= 0 || count > 10000 || width < 60 || !std::isfinite(width))
        return false;
    const int before = selected;
    selected = std::clamp(selected, 0, count - 1);
    PushID(label);
    const auto position = GetCursorScreenPos();
    const bool opened = InvisibleButton("choice", {width, 38});
    const auto &style = GetStyle();
    auto *draw = GetWindowDrawList();
    draw->AddRectFilled(position, {position.x + width, position.y + 38}, style.Field, style.Rounding);
    draw->AddRect(position, {position.x + width, position.y + 38},
                  IsItemFocused() ? style.Accent : style.Border, 1, style.Rounding);
    const auto caption = std::string(label) + ": " + (items[selected] ? items[selected] : "");
    draw->PushClipRect({position.x + 10, position.y}, {position.x + width - 30, position.y + 38});
    draw->AddText(GetFont(), style.FontSize, {position.x + 10, position.y + 10}, style.Text, caption.c_str());
    draw->PopClipRect();
    draw->AddText(GetFont(), style.FontSize, {position.x + width - 24, position.y + 10}, style.Muted, "v");
    {
        if (IsKeyPressed(VK_DOWN))
            selected = (selected + 1) % count;
        if (IsKeyPressed(VK_UP))
            selected = (selected + count - 1) % count;
    }
    if (opened)
        OpenPopup("items");
    if (BeginPopup("items", {width, std::min(count * 34.0f + 12, 284.0f)}, true)) {
        const auto size = GetWindowSize();
        const auto top = GetCursorScreenPos();
        SetCursorScreenPos({top.x + 6, top.y + 6});
        BeginChild("options", {size.x - 12, size.y - 12});
        for (int i = 0; i < count; ++i) {
            PushID(i);
            if (opened && i == selected)
                SetKeyboardFocusHere();
            if (Selectable(items[i] ? items[i] : "", selected == i, {size.x - 20, 34})) {
                selected = i;
                CloseCurrentPopup();
            }
            PopID();
        }
        EndChild();
        EndPopup();
    }
    PopID();
    return before != selected;
}

void ProgressBar(float fraction, Point size, const char *overlay) {
    const auto position = GetCursorScreenPos();
    const auto &style = GetStyle();
    if (!std::isfinite(fraction))
        fraction = 0;
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    auto *draw = GetWindowDrawList();
    draw->AddRectFilled(position, {position.x + size.x, position.y + size.y}, style.Field, style.Rounding);
    draw->PushClipRect(position, {position.x + size.x * fraction, position.y + size.y});
    draw->AddRectFilled(position, {position.x + size.x, position.y + size.y}, style.Accent, style.Rounding);
    draw->PopClipRect();
    if (overlay) {
        const auto extent = GetFont()->MeasureText(overlay, style.FontSize);
        draw->PushClipRect(position, {position.x + size.x, position.y + size.y});
        draw->AddText(GetFont(), style.FontSize,
                      {position.x + (size.x - extent.x) / 2, position.y + (size.y - extent.y) / 2},
                      style.Text, overlay);
        draw->PopClipRect();
    }
    Dummy(size);
}

}
