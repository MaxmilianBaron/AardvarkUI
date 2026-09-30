#include <aardvark/ui.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace aardvark::ui {

int DataTable(const char *id, TableColumn *columns, int count, int rows, int &selected, TableSort &sort,
              TableCell cell, void *user, Point size, float row_height) {
    if (!id || !columns || !cell || count <= 0 || count > 32 || rows < 0 || rows > 1000000 ||
        !std::isfinite(size.x) || !std::isfinite(size.y) || size.x > 32768 || size.y > 32768 ||
        size.x < count * 48.0f + 12 || size.y < 80 || !std::isfinite(row_height) || row_height < 20 ||
        static_cast<double>(rows) * row_height > 16777216)
        return 0;
    double total = 0;
    for (int i = 0; i < count; ++i) {
        if (!columns[i].label || !std::isfinite(columns[i].weight) || columns[i].weight <= 0)
            return 0;
        total += columns[i].weight;
    }
    std::array<float, 32> widths{};
    const float usable = size.x - 12;
    const float surplus = usable - count * 48;
    for (int i = 0; i < count; ++i)
        widths[i] = 48 + static_cast<float>(static_cast<double>(surplus) * columns[i].weight / total);
    const auto origin = GetCursorScreenPos();
    const auto &style = GetStyle();
    auto *draw = GetWindowDrawList();
    int events = 0;
    PushID(id);
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, style.Background, style.Rounding);
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + 36}, style.Field, style.Rounding);
    float x = origin.x;
    for (int col = 0; col < count; ++col) {
        PushID(col);
        SetCursorScreenPos({x + 1, origin.y + 1});
        if (InvisibleButton("heading", {widths[col] - 7, 34})) {
            sort.descending = sort.column == col && !sort.descending;
            sort.column = col;
            events |= SortChanged;
        }
        if (IsItemHovered() || IsItemFocused())
            draw->AddRectFilled({x + 1, origin.y + 1}, {x + widths[col] - 6, origin.y + 35}, style.Selection,
                                4);
        draw->PushClipRect({x + 9, origin.y + 2}, {x + widths[col] - 22, origin.y + 34});
        draw->AddText(GetFont(), style.FontSize, {x + 9, origin.y + (36 - style.FontSize) / 2},
                      IsItemDisabled() ? style.Muted : style.Text, columns[col].label);
        draw->PopClipRect();
        if (sort.column == col)
            draw->AddText(GetFont(), 14, {x + widths[col] - 20, origin.y + 11}, style.Accent,
                          sort.descending ? "v" : "^");
        if (col + 1 < count && surplus > 0) {
            SetCursorScreenPos({x + widths[col] - 5, origin.y + 1});
            InvisibleButton("resize", {8, 34});
            float delta = IsItemActive() && !IsItemActivated() ? GetIO().MouseDelta.x : 0;
            if (IsKeyPressed(VK_LEFT))
                delta -= 8;
            if (IsKeyPressed(VK_RIGHT))
                delta += 8;
            const float margin = std::min(0.01f, surplus / (count * 2));
            delta = std::clamp(delta, std::min(0.0f, 48 + margin - widths[col]),
                               std::max(0.0f, widths[col + 1] - 48 - margin));
            if (delta != 0) {
                widths[col] += delta;
                widths[col + 1] -= delta;
                for (int i = 0; i < count; ++i)
                    columns[i].weight = std::max(margin, widths[i] - 48);
                events |= ColumnResized;
            }
            draw->AddLine({x + widths[col], origin.y + 9}, {x + widths[col], origin.y + 27},
                          IsItemHovered() || IsItemActive() || IsItemFocused() ? style.Accent : style.Border);
        }
        x += widths[col];
        PopID();
    }
    const auto body = Point{origin.x, origin.y + 36};
    const float height = size.y - 36;
    SetCursorScreenPos(body);
    BeginChild("body", {size.x, height});
    SetContentHeight(rows * row_height);
    SetCursorScreenPos(body);
    const bool click = InvisibleButton("selection", {usable, height});
    const bool focused = IsItemFocused();
    const bool hovered = IsItemHovered();
    const bool disabled = IsItemDisabled();
    const int before = selected;
    selected = selected < 0 || selected >= rows ? -1 : selected;
    bool navigate = false;
    const int page = std::max(1, static_cast<int>(height / row_height));
    if (rows > 0) {
        if (IsKeyPressed(VK_UP)) {
            selected = selected < 0 ? rows - 1 : std::max(0, selected - 1);
            navigate = true;
        }
        if (IsKeyPressed(VK_DOWN)) {
            selected = std::min(rows - 1, selected + 1);
            navigate = true;
        }
        if (IsKeyPressed(VK_HOME)) {
            selected = 0;
            navigate = true;
        }
        if (IsKeyPressed(VK_END)) {
            selected = rows - 1;
            navigate = true;
        }
        if (IsKeyPressed(VK_PRIOR) || IsKeyPressed(VK_NEXT)) {
            selected =
                std::clamp(std::max(0, selected) + (IsKeyPressed(VK_PRIOR) ? -page : page), 0, rows - 1);
            navigate = true;
        }
        if (navigate) {
            const float top = selected * row_height;
            if (top < GetScrollY())
                SetScrollY(top);
            else if (top + row_height > GetScrollY() + height)
                SetScrollY(top + row_height - height);
        }
    }
    const float hit = std::floor((GetIO().MousePos.y - body.y + GetScrollY()) / row_height);
    const int hovered_row =
        hovered && std::isfinite(hit) && hit >= 0 && hit < rows ? static_cast<int>(hit) : -1;
    if (click && hovered_row >= 0 && !IsKeyPressed(VK_RETURN) && !IsKeyPressed(VK_SPACE))
        selected = hovered_row;
    if (selected != before)
        events |= SelectionChanged;
    const float top = body.y - GetScrollY();
    SetCursorScreenPos({body.x, top});
    const auto visible = VisibleRows(rows, row_height);
    for (int row = visible.first; row < visible.last; ++row) {
        const float y = top + row * row_height;
        if (row == selected || row == hovered_row || row % 2)
            draw->AddRectFilled({body.x + 1, y}, {body.x + usable, y + row_height},
                                row == selected ? style.Selection : style.Field);
        if (row == selected && focused)
            draw->AddRectFilled({body.x + 1, y}, {body.x + 3, y + row_height}, style.Accent);
        x = body.x;
        for (int col = 0; col < count; ++col) {
            draw->PushClipRect({x + 9, y}, {x + widths[col] - 8, y + row_height});
            const auto *text = cell(row, col, user);
            if (text)
                draw->AddText(GetFont(), style.FontSize, {x + 9, y + (row_height - style.FontSize) / 2},
                              disabled          ? style.Muted
                              : row == selected ? style.Accent
                                                : style.Text,
                              text);
            draw->PopClipRect();
            x += widths[col];
        }
    }
    if (!rows)
        draw->AddText(GetFont(), style.FontSize, {body.x + 12, body.y + 16}, style.Muted, "No entries");
    EndChild();
    draw->AddRect(origin, {origin.x + size.x, origin.y + size.y}, style.Border, 1, style.Rounding);
    PopID();
    SetCursorScreenPos(origin);
    Dummy(size);
    return events;
}

}
