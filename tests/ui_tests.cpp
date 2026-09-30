#include <aardvark/ui.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

namespace ui = aardvark::ui;
static int checks = 0;
#define CHECK(expression)                                                                                    \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(expression)) {                                                                                 \
            std::fprintf(stderr, "Line %d: %s\n", __LINE__, #expression);                                    \
            std::exit(1);                                                                                    \
        }                                                                                                    \
    } while (false)

static void geometry() {
    ui::DrawList draw;
    draw.Clear({100, 100});
    draw.AddRectFilled({0, 0}, {30, 30}, ui::White);
    draw.AddRectFilled({30, 30}, {60, 60}, ui::White);
    CHECK(draw.vertices.size() == 12 && draw.commands.size() == 1);
    draw.PushClipRect({20, 20}, {80, 80});
    draw.PushClipRect({0, 0}, {50, 50});
    draw.AddRectFilled({0, 0}, {50, 50}, ui::White);
    CHECK(draw.commands.back().clip.min.x == 20 && draw.commands.back().clip.max.x == 50);
    const auto count = draw.vertices.size();
    draw.AddRectFilled({80, 80}, {90, 90}, ui::White);
    draw.AddRectFilled({0, 0}, {50, 50}, 0);
    draw.AddLine({0, 0}, {0, 0}, ui::White);
    draw.AddRectFilled({std::numeric_limits<float>::quiet_NaN(), 0}, {50, 50}, ui::White);
    CHECK(draw.vertices.size() == count);
    draw.PopClipRect();
    draw.PopClipRect();
    draw.PopClipRect();
    draw.AddLine({1, 1}, {10, 10}, ui::White, 2);
    CHECK(draw.vertices.size() == count + 6);
    for (const auto &vertex : draw.vertices)
        CHECK(std::isfinite(vertex.x) && std::isfinite(vertex.y));
}

static void frame_begin() {
    ui::NewFrame();
    ui::SetNextWindowPos({0, 0});
    ui::SetNextWindowSize({300, 300});
    ui::Begin("test");
}
static void frame_end() {
    ui::End();
    ui::EndFrame();
}
static bool button_frame(bool disabled = false) {
    frame_begin();
    ui::BeginDisabled(disabled);
    const bool result = ui::InvisibleButton("action", {100, 30});
    ui::EndDisabled();
    frame_end();
    return result;
}
static void input() {
    auto *context = ui::CreateContext();
    ui::SetCurrentContext(context);
    ui::GetIO().DisplaySize = {640, 480};
    CHECK(!button_frame());
    ui::Message(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(10, 10));
    CHECK(!button_frame() && ui::IsItemActive() && ui::IsItemActivated());
    ui::Message(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
    CHECK(button_frame() && !ui::IsItemActive());
    CHECK(!button_frame());
    ui::Message(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(10, 10));
    CHECK(!button_frame(true) && !ui::IsItemActive());
    ui::Message(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
    CHECK(!button_frame(true));
    ui::Message(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(10, 10));
    button_frame();
    ui::Message(nullptr, WM_KILLFOCUS, 0, 0);
    ui::Message(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
    CHECK(!button_frame());
    ui::SetInputEnabled(false);
    ui::Message(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(10, 10));
    CHECK(!button_frame() && !ui::IsItemActive());
    ui::SetInputEnabled(true);
    ui::Message(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(50, 34));
    frame_begin();
    float value = 0;
    CHECK(ui::SliderFloat("amount", value, 0, 10, 100) && value == 5);
    frame_end();
    ui::ClearInput();
    frame_begin();
    ui::BeginChild("scroll", {100, 100});
    ui::Dummy({100, 500});
    ui::EndChild();
    frame_end();
    ui::ClearInput();
    for (int pass = 0; pass < 2; ++pass) {
        if (pass)
            ui::Message(nullptr, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
        frame_begin();
        ui::PushID(1);
        ui::BeginChild("repeated", {100, 100});
        if (pass)
            CHECK(ui::GetCursorScreenPos().y < 0);
        ui::Dummy({100, 500});
        ui::EndChild();
        ui::PopID();
        ui::SetCursorScreenPos({200, 0});
        ui::PushID(2);
        ui::BeginChild("repeated", {100, 100});
        CHECK(ui::GetCursorScreenPos().y == 0);
        ui::Dummy({100, 500});
        ui::EndChild();
        ui::PopID();
        frame_end();
    }
    frame_begin();
    ui::SetWindowPos({20, 30});
    CHECK(ui::GetCursorScreenPos().x == 20 && ui::GetCursorScreenPos().y == 30);
    frame_end();
    frame_begin();
    ui::BeginChild("scroll", {100, 100});
    ui::Dummy({100, 500});
    ui::EndChild();
    frame_end();
    ui::Message(nullptr, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
    frame_begin();
    ui::BeginChild("scroll", {100, 100});
    CHECK(ui::GetCursorScreenPos().y < 0);
    ui::Dummy({100, 500});
    ui::EndChild();
    frame_end();
    auto *other = ui::CreateContext();
    ui::SetCurrentContext(other);
    CHECK(ui::GetIO().DisplaySize.x == 0 && !ui::GetIO().MouseDown[0]);
    ui::DestroyContext(other);
    ui::SetCurrentContext(context);
    CHECK(ui::GetIO().DisplaySize.x == 640);
    ui::DestroyContext(context);
}

static void key(unsigned value, bool control = false, bool shift = false) {
    if (control)
        ui::Message(nullptr, WM_KEYDOWN, VK_CONTROL, 0);
    if (shift)
        ui::Message(nullptr, WM_KEYDOWN, VK_SHIFT, 0);
    ui::Message(nullptr, WM_KEYDOWN, value, 0);
    ui::Message(nullptr, WM_KEYUP, value, 0);
    if (shift)
        ui::Message(nullptr, WM_KEYUP, VK_SHIFT, 0);
    if (control)
        ui::Message(nullptr, WM_KEYUP, VK_CONTROL, 0);
}
static bool clipboard_read(std::string &text, void *user) {
    text = *static_cast<std::string *>(user);
    return true;
}
static bool clipboard_write(const std::string &text, void *user) {
    *static_cast<std::string *>(user) = text;
    return true;
}
static void editing() {
    auto *context = ui::CreateContext();
    ui::SetCurrentContext(context);
    ui::GetIO().DisplaySize = {640, 480};
    std::string first, second, clipboard;
    ui::SetClipboardHandlers(clipboard_read, clipboard_write, &clipboard);
    auto frame = [&](bool focus = false, int flags = 0, bool disabled = false) {
        frame_begin();
        if (focus)
            ui::SetKeyboardFocusHere();
        ui::BeginDisabled(disabled);
        ui::InputText("first", first, 220, 12, flags);
        ui::EndDisabled();
        ui::Spacing();
        ui::InputText("second", second, 220, 12);
        frame_end();
    };
    frame(true);
    CHECK(ui::GetIO().WantTextInput && ui::GetIO().WantCaptureKeyboard);
    ui::Message(nullptr, WM_CHAR, 'a', 0);
    ui::Message(nullptr, WM_CHAR, 'b', 0);
    frame();
    CHECK(first == "ab" && second.empty());
    key(VK_HOME);
    key(VK_RIGHT, false, true);
    ui::Message(nullptr, WM_CHAR, 'X', 0);
    frame();
    CHECK(first == "Xb");
    key('Z', true);
    frame();
    CHECK(first == "ab");
    key('Y', true);
    frame();
    CHECK(first == "Xb");
    key('A', true);
    key('C', true);
    frame();
    CHECK(clipboard == "Xb");
    clipboard = "Hello\nworld! long";
    key('V', true);
    frame();
    CHECK(first == "Helloworld! " && first.size() == 12);
    key('A', true);
    ui::Message(nullptr, WM_CHAR, 0xd83d, 0);
    ui::Message(nullptr, WM_CHAR, 0xde42, 0);
    frame();
    CHECK(first == "\xf0\x9f\x99\x82");
    key(VK_BACK);
    frame();
    CHECK(first.empty());
    ui::Message(nullptr, WM_CHAR, '1', 0);
    key(VK_TAB);
    ui::Message(nullptr, WM_CHAR, '2', 0);
    frame();
    CHECK(first == "1" && second == "2");
    key(VK_TAB, false, true);
    ui::Message(nullptr, WM_CHAR, '3', 0);
    frame();
    CHECK(first == "13" && second == "2");
    key('A', true);
    ui::Message(nullptr, WM_CHAR, 'Q', 0);
    frame(false, ui::TextReadOnly);
    CHECK(first == "13");
    clipboard = "retained";
    key('A', true);
    key('X', true);
    frame(false, ui::TextPassword);
    CHECK(clipboard == "retained" && first == "13");
    key(VK_BACK);
    frame(false, 0, true);
    CHECK(first == "13");
    frame(true);
    ui::Message(nullptr, WM_KILLFOCUS, 0, 0);
    ui::Message(nullptr, WM_CHAR, 'x', 0);
    frame();
    CHECK(first == "13");
    frame(true);
    key('A', true);
    clipboard = "12345678901\xf0\x9f\x99\x82";
    key('V', true);
    frame();
    CHECK(first == "12345678901");
    key('A', true);
    ui::Message(nullptr, WM_CHAR, 0xd800, 0);
    ui::Message(nullptr, WM_CHAR, 'x', 0);
    frame();
    CHECK(first == "\xef\xbf\xbdx");
    ui::DestroyContext(context);
}
static void navigation() {
    auto *context = ui::CreateContext();
    ui::SetCurrentContext(context);
    ui::GetIO().DisplaySize = {640, 480};
    int clicks = 0;
    bool checked = false;
    float amount = 0.5f;
    auto frame = [&] {
        frame_begin();
        if (ui::Button("first"))
            ++clicks;
        ui::Checkbox("check", checked);
        ui::SliderFloat("slider", amount, 0, 1);
        frame_end();
    };
    frame();
    key(VK_TAB);
    key(VK_RETURN);
    frame();
    CHECK(clicks == 1);
    key(VK_TAB);
    key(VK_SPACE);
    frame();
    CHECK(checked && clicks == 1);
    key(VK_TAB);
    key(VK_RIGHT);
    frame();
    CHECK(std::abs(amount - 0.51f) < 0.0001f);
    key(VK_END);
    frame();
    CHECK(amount == 1);
    key(VK_HOME);
    key(VK_TAB);
    key(VK_RETURN);
    key(VK_TAB);
    key(VK_SPACE);
    frame();
    CHECK(amount == 0 && clicks == 2 && !checked);
    frame_begin();
    CHECK(ui::BeginTable("table", 2, 200));
    ui::TableNextColumn();
    CHECK(ui::GetCursorScreenPos().x == 0);
    ui::Dummy({20, 40});
    ui::TableNextColumn();
    CHECK(ui::GetCursorScreenPos().x == 100 && ui::GetCursorScreenPos().y == 0);
    ui::Dummy({20, 20});
    ui::TableNextColumn();
    CHECK(ui::GetCursorScreenPos().x == 0 && ui::GetCursorScreenPos().y == 48);
    ui::Dummy({20, 30});
    ui::EndTable();
    CHECK(ui::GetCursorScreenPos().x == 0 && ui::GetCursorScreenPos().y == 78);
    frame_end();
    ui::DestroyContext(context);
}
static void nested_scroll() {
    auto *context = ui::CreateContext();
    ui::SetCurrentContext(context);
    ui::GetIO().DisplaySize = {640, 480};
    ui::Message(nullptr, WM_MOUSEMOVE, 0, MAKELPARAM(30, 30));
    float parent = 0, child = 0;
    auto frame = [&](bool focus = false) {
        frame_begin();
        ui::BeginChild("parent", {200, 180});
        parent = ui::GetCursorScreenPos().y;
        ui::BeginChild("child", {150, 100});
        child = ui::GetCursorScreenPos().y - parent;
        if (focus)
            ui::SetKeyboardFocusHere();
        ui::Selectable("selected", true, {120, 30});
        ui::Dummy({100, 470});
        ui::EndChild();
        ui::Dummy({200, 400});
        ui::EndChild();
        frame_end();
    };
    frame(true);
    ui::Message(nullptr, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
    frame();
    CHECK(parent == 0 && child == -42);
    frame();
    CHECK(parent == 0 && child == -42);
    ui::Message(nullptr, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-10 * WHEEL_DELTA)), 0);
    frame();
    CHECK(parent == 0 && child == -400);
    ui::Message(nullptr, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
    frame();
    CHECK(parent == -42 && child == -400);
    ui::DestroyContext(context);
}

static void popup() {
    auto *context = ui::CreateContext();
    ui::SetCurrentContext(context);
    ui::GetIO().DisplaySize = {640, 480};
    int choice = 0, underlying = 0;
    const char *items[] = {"One", "Two", "Three"};
    auto frame = [&] {
        frame_begin();
        ui::Combo("mode", choice, items, 3, 200);
        ui::SetCursorScreenPos({0, 180});
        if (ui::Button("under"))
            ++underlying;
        frame_end();
    };
    frame();
    ui::Message(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(20, 15));
    frame();
    ui::Message(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(20, 15));
    frame();
    ui::Message(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(20, 94));
    frame();
    ui::Message(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(20, 94));
    frame();
    CHECK(choice == 1);
    frame();
    ui::Message(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(20, 15));
    frame();
    ui::Message(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(20, 15));
    frame();
    ui::Message(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(20, 195));
    frame();
    ui::Message(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(20, 195));
    frame();
    CHECK(underlying == 0);
    ui::DestroyContext(context);
}

int main() {
    geometry();
    input();
    editing();
    navigation();
    nested_scroll();
    popup();
    std::printf("%d checks passed\n", checks);
}
