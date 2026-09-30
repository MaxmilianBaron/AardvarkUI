#include <aardvark/ui.hpp>

int main() {
    namespace ui = aardvark::ui;
    auto *context = ui::CreateContext();
    ui::SetCurrentContext(context);
    ui::GetIO().DisplaySize = {640, 480};
    ui::SetTheme(ui::Theme::Dark);
    ui::NewFrame();
    ui::SetNextWindowSize({640, 480});
    ui::Begin("Consumer");
    ui::TableColumn columns[] = {{"Name", 1}};
    ui::TableSort sort;
    int selected = -1, calls = 0;
    ui::DataTable("table", columns, 1, 100000, selected, sort,
                  [](int, int, void *user) -> const char * {
                      ++*static_cast<int *>(user);
                      return "Record";
                  },
                  &calls, {200, 200});
    ui::End();
    ui::EndFrame();
    ui::DestroyContext(context);
    return calls > 0 && calls <= 6 ? 0 : 1;
}
