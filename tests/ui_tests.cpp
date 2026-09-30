#include <aardvark/ui.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

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

int main() {
    geometry();
    input();
    std::printf("%d checks passed\n", checks);
}
