#include <aardvark/ui.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace ui = aardvark::ui;
static unsigned checks = 0;
#define CHECK(expression)                                                                                    \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(expression)) {                                                                                 \
            std::fprintf(stderr, "Line %d: %s\n", __LINE__, #expression);                                    \
            std::exit(1);                                                                                    \
        }                                                                                                    \
    } while (false)

struct Fixture {
    ui::Context *context = ui::CreateContext();
    ui::TableColumn columns[3]{{"Name", 1}, {"Type", 1}, {"Status", 1}};
    ui::TableSort sort;
    int selected = -1, rows = 100000;
    bool disabled = false;
    std::vector<int> requested;
    std::size_t vertices = 0;
    Fixture() {
        ui::SetCurrentContext(context);
        ui::GetIO().DisplaySize = {640, 480};
    }
    ~Fixture() {
        ui::DestroyContext(context);
    }
    static const char *cell(int row, int col, void *user) {
        auto &test = *static_cast<Fixture *>(user);
        CHECK(row >= 0 && row < test.rows && col >= 0 && col < 3);
        if (col == 0)
            test.requested.push_back(row);
        return "Value";
    }
    int frame() {
        requested.clear();
        ui::NewFrame();
        ui::SetNextWindowPos({0, 0});
        ui::SetNextWindowSize({600, 400});
        ui::Begin("Data");
        ui::BeginDisabled(disabled);
        int result = ui::DataTable("records", columns, 3, rows, selected, sort, cell, this, {300, 200}, 32);
        ui::EndDisabled();
        CHECK(ui::GetCursorScreenPos().y == 200);
        vertices = ui::GetWindowDrawList()->vertices.size();
        ui::End();
        ui::EndFrame();
        return result;
    }
    void key(unsigned key) {
        ui::Message(nullptr, WM_KEYDOWN, key, 0);
        ui::Message(nullptr, WM_KEYUP, key, 0);
    }
    int click(int x, int y) {
        ui::Message(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y));
        frame();
        ui::Message(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        return frame();
    }
};

static void selection_and_bounds() {
    Fixture test;
    CHECK(test.frame() == 0);
    CHECK((test.requested == std::vector<int>{0, 1, 2, 3, 4, 5}));
    CHECK(test.vertices < 3000);
    CHECK(test.click(20, 52) & ui::SelectionChanged);
    CHECK(test.selected == 0);
    test.key(VK_END);
    CHECK(test.frame() & ui::SelectionChanged);
    CHECK(test.selected == 99999 && test.requested.back() == 99999 && test.requested.size() <= 7);
    test.key(VK_UP);
    test.frame();
    CHECK(test.selected == 99998);
    test.key(VK_HOME);
    test.frame();
    CHECK(test.selected == 0 && test.requested.front() == 0);
    test.key(VK_NEXT);
    test.frame();
    CHECK(test.selected == 5);
    test.key(VK_PRIOR);
    test.frame();
    CHECK(test.selected == 0);
    ui::Message(nullptr, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
    test.frame();
    CHECK(test.requested.front() == 1);
    test.click(20, 52);
    CHECK(test.selected == 1);
    test.key(VK_END);
    test.frame();
    test.rows = 2;
    CHECK(test.frame() & ui::SelectionChanged);
    CHECK(test.selected == -1 && (test.requested == std::vector<int>{0, 1}));
    test.rows = 0;
    test.frame();
    CHECK(test.selected == -1 && test.requested.empty());
    test.rows = 100000;
    test.disabled = true;
    test.click(20, 52);
    test.key(VK_END);
    test.frame();
    CHECK(test.selected == -1);
}

static void headers() {
    Fixture test;
    test.frame();
    CHECK(test.click(20, 18) & ui::SortChanged);
    CHECK(test.sort.column == 0 && !test.sort.descending);
    CHECK(test.click(20, 18) & ui::SortChanged);
    CHECK(test.sort.descending);
    test.key(VK_TAB);
    test.key(VK_RIGHT);
    CHECK(test.frame() & ui::ColumnResized);
    CHECK(test.columns[0].weight > test.columns[1].weight);
    const float previous = test.columns[0].weight;
    test.key(VK_LEFT);
    CHECK(test.frame() & ui::ColumnResized);
    CHECK(test.columns[0].weight < previous);
    test.key(VK_TAB);
    test.key(VK_RETURN);
    CHECK(test.frame() & ui::SortChanged);
    CHECK(test.sort.column == 1 && !test.sort.descending);
    ui::Message(nullptr, WM_LBUTTONDOWN, 0, MAKELPARAM(96, 18));
    test.frame();
    ui::Message(nullptr, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(125, 18));
    CHECK(test.frame() & ui::ColumnResized);
    CHECK(test.columns[0].weight > test.columns[1].weight);
    ui::Message(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(125, 18));
    test.frame();
    for (const auto &column : test.columns)
        CHECK(column.weight > 0 && std::isfinite(column.weight));
}

static void visible_range_and_theme() {
    Fixture test;
    ui::NewFrame();
    ui::SetNextWindowSize({300, 300});
    ui::Begin("Clip");
    ui::BeginChild("list", {200, 100});
    ui::SetContentHeight(3200000);
    ui::SetScrollY(1600000);
    auto range = ui::VisibleRows(100000, 32);
    CHECK(range.first == 50000 && range.last == 50004);
    ui::SetScrollY(9999999);
    range = ui::VisibleRows(100000, 32);
    CHECK(range.last == 100000 && range.last - range.first <= 4);
    ui::SetContentHeight(64);
    CHECK(ui::GetScrollY() == 0);
    CHECK(ui::VisibleRows(2, 32).last == 2);
    CHECK(ui::VisibleRows(-1, 32).last == 0);
    CHECK(ui::VisibleRows(1000001, 32).last == 0);
    CHECK(ui::VisibleRows(2, std::numeric_limits<float>::quiet_NaN()).last == 0);
    ui::EndChild();
    ui::End();
    ui::EndFrame();
    const auto text = ui::GetStyle().Text;
    ui::GetStyle().FontSize = 20;
    ui::SetTheme(ui::Theme::Dark);
    CHECK(ui::GetStyle().Text != text && ui::GetStyle().FontSize == 20);
    ui::SetTheme(ui::Theme::Light);
    CHECK(ui::GetStyle().Text == text && ui::GetStyle().FontSize == 20);
    ui::DrawList draw;
    draw.Clear({100, 100});
    draw.AddLine({200, 200}, {300, 300}, ui::White, 2);
    CHECK(draw.vertices.empty());
    draw.AddLine({-10, 50}, {110, 50}, ui::White, 2);
    CHECK(draw.vertices.size() == 6);
}

static void benchmark() {
    Fixture test;
    test.rows = 100;
    test.frame();
    auto small_rows = test.requested.size();
    auto small_vertices = test.vertices;
    test.rows = 100000;
    test.frame();
    CHECK(test.requested.size() == small_rows && test.vertices == small_vertices);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i)
        test.frame();
    const auto elapsed =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    std::printf("100,000 rows: %zu submitted rows, %zu vertices, %.2f us/frame (headless, 1000 frames)\n",
                test.requested.size(), test.vertices, elapsed / 1000);
}

static void invalid_arguments() {
    Fixture test;
    ui::NewFrame();
    ui::SetNextWindowSize({600, 400});
    ui::Begin("Invalid");
    auto *draw = ui::GetWindowDrawList();
    const auto reject = [&](const char *id, ui::TableColumn *columns, int count, int rows, ui::TableCell cell,
                            ui::Point size, float height) {
        CHECK(ui::DataTable(id, columns, count, rows, test.selected, test.sort, cell, &test, size, height) ==
              0);
        CHECK(test.requested.empty() && draw->vertices.empty());
        CHECK(test.selected == -1 && test.sort.column == -1);
        CHECK(ui::GetCursorScreenPos().x == 0 && ui::GetCursorScreenPos().y == 0);
    };
    reject(nullptr, test.columns, 3, 100, Fixture::cell, {300, 200}, 32);
    reject("rows", nullptr, 3, 100, Fixture::cell, {300, 200}, 32);
    reject("rows", test.columns, 3, 100, nullptr, {300, 200}, 32);
    for (const int count : {0, -1, 33})
        reject("rows", test.columns, count, 100, Fixture::cell, {300, 200}, 32);
    for (const int rows : {-1, 1000001, 600000})
        reject("rows", test.columns, 3, rows, Fixture::cell, {300, 200}, 32);
    for (const float value :
         {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        reject("rows", test.columns, 3, 100, Fixture::cell, {value, 200}, 32);
        reject("rows", test.columns, 3, 100, Fixture::cell, {300, value}, 32);
        reject("rows", test.columns, 3, 100, Fixture::cell, {300, 200}, value);
        test.columns[0].weight = value;
        reject("rows", test.columns, 3, 100, Fixture::cell, {300, 200}, 32);
        test.columns[0].weight = 1;
    }
    test.columns[0].label = nullptr;
    reject("rows", test.columns, 3, 100, Fixture::cell, {300, 200}, 32);
    ui::End();
    ui::EndFrame();
}

int main() {
    selection_and_bounds();
    headers();
    visible_range_and_theme();
    invalid_arguments();
    benchmark();
    std::printf("%u table checks passed\n", checks);
}
