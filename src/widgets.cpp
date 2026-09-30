#include <aardvark/ui.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace aardvark::ui {

void Text(const char *text, float size, Color color, float wrap) {
    if (!text)
        return;
    auto *font = GetFont();
    GetWindowDrawList()->AddText(font, size, GetCursorScreenPos(), color, text, nullptr, wrap);
    Dummy(font->MeasureText(text, size, wrap));
}

bool Button(const char *label, Point size) {
    const auto position = GetCursorScreenPos();
    const bool clicked = InvisibleButton(label, size);
    auto *draw = GetWindowDrawList();
    const auto color = IsItemActive()    ? RGBA(35, 89, 151)
                       : IsItemHovered() ? RGBA(63, 123, 188)
                                         : RGBA(42, 71, 107);
    draw->AddRectFilled(position, {position.x + size.x, position.y + size.y}, color);
    const auto text = GetFont()->MeasureText(label, 18);
    draw->PushClipRect({position.x + 6, position.y}, {position.x + size.x - 6, position.y + size.y});
    draw->AddText(GetFont(), 18, {position.x + (size.x - text.x) / 2, position.y + (size.y - text.y) / 2},
                  White, label);
    draw->PopClipRect();
    return clicked;
}

bool Checkbox(const char *label, bool &value) {
    const auto position = GetCursorScreenPos();
    const auto text = GetFont()->MeasureText(label, 18);
    const bool clicked = InvisibleButton(label, {34 + text.x, 28});
    if (clicked)
        value = !value;
    auto *draw = GetWindowDrawList();
    draw->AddRectFilled(position, {position.x + 22, position.y + 22},
                        IsItemHovered() ? RGBA(63, 123, 188) : RGBA(42, 71, 107));
    if (value) {
        draw->AddLine({position.x + 5, position.y + 11}, {position.x + 10, position.y + 16}, White, 2);
        draw->AddLine({position.x + 10, position.y + 16}, {position.x + 18, position.y + 6}, White, 2);
    }
    draw->AddText(GetFont(), 18, {position.x + 34, position.y + 2}, RGBA(218, 225, 235), label);
    return clicked;
}

bool SliderFloat(const char *label, float &value, float min, float max, float width) {
    if (!std::isfinite(min) || !std::isfinite(max) || max <= min || !std::isfinite(max - min) ||
        !std::isfinite(width) || width < 40)
        return false;
    const auto position = GetCursorScreenPos();
    const auto before = value;
    if (!std::isfinite(value))
        value = min;
    value = std::clamp(value, min, max);
    InvisibleButton(label, {width, 52});
    if (IsItemActive() || IsItemActivated()) {
        const auto fraction = std::clamp((GetIO().MousePos.x - position.x) / width, 0.0f, 1.0f);
        value = min + fraction * (max - min);
    }
    auto *draw = GetWindowDrawList();
    draw->PushClipRect(position, {position.x + width, position.y + 52});
    draw->AddText(GetFont(), 18, position, RGBA(218, 225, 235), label);
    char number[32]{};
    std::snprintf(number, sizeof(number), "%.2f", value);
    const auto text = GetFont()->MeasureText(number, 18);
    draw->AddText(GetFont(), 18, {position.x + width - text.x, position.y}, RGBA(113, 189, 255), number);
    const float fraction = (value - min) / (max - min);
    draw->AddRectFilled({position.x, position.y + 33}, {position.x + width, position.y + 39},
                        RGBA(42, 55, 72));
    draw->AddRectFilled({position.x, position.y + 33}, {position.x + width * fraction, position.y + 39},
                        RGBA(83, 163, 234));
    const float knob = position.x + std::clamp(width * fraction, 4.0f, width - 4);
    draw->AddRectFilled({knob - 4, position.y + 29}, {knob + 4, position.y + 43}, RGBA(189, 225, 255));
    draw->PopClipRect();
    return before != value;
}

}
