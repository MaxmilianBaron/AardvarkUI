# Integration

## CMake

Use `add_subdirectory(AardvarkUI)` and link `AardvarkUI::AardvarkUI`, or install a built configuration:

```powershell
cmake --install build --config Release --prefix package
```

Consumers use `find_package(AardvarkUI 0.3 CONFIG REQUIRED)` with the installation in `CMAKE_PREFIX_PATH`. Match the target architecture and C++ runtime configuration. The static target carries its Direct3D 9, GDI and User32 dependencies.

## Context and rendering

Create a context after creating a Win32 window and classic Direct3D 9 device:

```cpp
#include <aardvark/ui.hpp>
namespace ui = aardvark::ui;

auto* context = ui::CreateContext(window, device);
ui::SetCurrentContext(context);
if (!ui::LoadFont(L"Cascadia Code", 40)) {
    ui::DestroyContext(context);
    return;
}
```

The context keeps a COM reference to the device. Caller-owned image textures must remain valid until their draw commands have been flushed. Use the context and device on one UI thread. The explicit current context is not thread-local.

Each frame, between the device's `BeginScene` and `EndScene`:

```cpp
ui::NewFrame();
ui::SetNextWindowPos({24, 24});
ui::SetNextWindowSize({360, 220});
ui::Begin("Tools");
ui::InputText("Name", name, 280, 128);
ui::Spacing();
if (ui::Button("Apply")) {
    apply_changes();
}
ui::SameLine();
ui::Checkbox("Enabled", enabled);
ui::End();
ui::Render();
```

Windows are layout and clipping regions; draw backgrounds with `GetWindowDrawList()`. Positions, font sizes and extents are client pixels. The host owns DPI policy. Pair each `Begin`/`BeginChild` with `End`/`EndChild`; pair successful `BeginTable`/`BeginPopup` calls with their corresponding end functions.

`DrawList` supports rounded rectangles, circles, lines, images and text. Clip rectangles nest. Consecutive commands sharing a texture and clip are batched. Popups use the foreground list. `Flush` draws and clears the lists while restoring device state; `Render` also ends the input frame. Headless tests can use `EndFrame` without a device.

The GDI font rasterizer caches glyphs in managed textures, bounded to 4,096 glyphs and 16 atlas pages per font. No font file is bundled. Windows selects a substitute when the requested family is unavailable. Load one font per context and vary its drawn size. Managed textures survive a normal D3D9 `Reset`; recreate the context when replacing the device. Destroy the context before the application's final device release.

## Input

Always forward window messages to `ui::Message(window, message, wparam, lparam)`, including when the UI does not currently capture input. Use `GetIO().WantCaptureMouse`, `WantCaptureKeyboard` and `WantTextInput` to keep captured input away from application actions. These flags describe the most recently built frame; they do not replace the host's window procedure. Continue normal OS message processing. Return `TRUE` for the `WM_UNICHAR`/`UNICODE_NOCHAR` capability probe if accepting Unicode messages.

Focus loss and cancelled mouse capture clear pressed state. `SetInputEnabled(false)`, `NoInputs` and `BeginDisabled` prevent interaction. Tab/Shift+Tab traverse controls; Enter/Space activate them. Sliders accept arrows and Home/End. Open combos accept Up/Down, Home/End, Tab and Enter; Escape cancels. Closing a popup returns focus to its opener on the following frame. `SetKeyboardFocusHere()` focuses the next enabled control. `InvisibleButton` can opt out of keyboard focus for pointer-only handles. Wheel events go to the deepest hovered scrollable panel, then its parent when the child reaches its limit.

`InputText` edits a caller-owned, single-line UTF-8 `std::string`. Supply valid single-line UTF-8 without embedded NUL characters. The byte limit caps insertions without splitting a code point; lowering it does not silently truncate existing content. Fields or limits over 1 MiB are rejected.

Text input supports selection, dragging, Home/End, Ctrl+arrows, Ctrl+A/C/X/V and Ctrl+Z/Y/Shift+Z. Undo/redo history is bounded and discarded when the field stops being submitted. `TextReadOnly` prevents edits; `TextPassword` masks display and disables copying/cutting. It is not secure storage and does not erase the caller's string. `TextEnterReturnsTrue` changes the return value from "edited" to "submitted".

Default clipboard access uses Win32 Unicode text. `SetClipboardHandlers` replaces both operations; callbacks and their user pointer must remain valid while installed. UTF-16 character messages are converted to UTF-8. Navigation operates on code points, not grapheme clusters. IME composition UI, bidirectional layout and complex-script shaping are not implemented.

## Layout and style

Widgets advance the cursor vertically. `SameLine`, `Spacing`, `Separator`, `Dummy` and `SetCursorScreenPos` support custom layouts. `BeginTable` creates equal-width columns; call `TableNextColumn` before each cell. It does not sort data or resize columns. Use `BeginChild` for scrolling.

Labels and IDs must be stable and null-terminated. Scope repeated labels with `PushID`/`PopID`. Tables scope cell IDs automatically. Submit windows and controls once per frame in a stable order. `OpenPopup` opens one popup at the last item's edge; clicking outside or pressing Escape closes it. `BeginPopup(id, size, true)` enables arrow/Home/End navigation for menus; leave the third argument false for popups containing editors. Nesting popups is not supported.

`GetStyle()` exposes colors, font size, spacing and rounding. The default is a light palette with dark buttons and green accents. `Text` uses the style text color when its color argument is zero. The demonstration icon is embedded only in the example executable; it is not a runtime library dependency.

`SetTheme(Theme::Light)` and `SetTheme(Theme::Dark)` replace palette colors while retaining font size, spacing and rounding. `IsItemDisabled()` lets custom controls display disabled state.

## Large data tables

`DataTable` renders a fixed-height row view over caller-owned data. It calls the cell provider only for visible rows. Headers remain above the scrolling body, support sorting requests, and expose resize handles operable by drag or Left/Right. The row area is one keyboard focus stop: Up/Down, Page Up/Down and Home/End select and reveal rows, including those never previously drawn.

```cpp
ui::TableColumn columns[] = {{"Name", 2}, {"Status", 1}};
ui::TableSort sort;
int selected = -1;

// Keep columns, sort and selection between frames.
const int events = ui::DataTable("records", columns, 2, row_count, selected,
                                sort, read_cell, model, {520, 300});
if (events & ui::SortChanged)
    reorder_model(sort.column, sort.descending);
```

The provider signature is `const char* read_cell(int row, int column, void* model)`. Return null for an empty cell or null-terminated UTF-8 valid until the next provider call. Do not change UI context or emit widgets inside the provider. Sorting and filtering belong to the caller; apply them only when the query or sort changes, not for every row on every frame. Preserve selection by a data identity when changing row order; `selected` itself is an index into the current view.

Column weights divide remaining width after a 48-pixel minimum per column. Resize edits the caller's weights. There are at most 32 columns and 1,000,000 rows, with total row height at most 16,777,216 pixels. Supported extents are at least `48 * columns + 12` wide, at least 80 high, and no more than 32,768 on either axis. Row height must be at least 20 pixels. Invalid arguments return zero without drawing or calling the provider. Flags report `SelectionChanged`, `SortChanged` and `ColumnResized`; an invalid selection becomes -1 when the data shrinks.

For custom fixed-height views, `SetContentHeight(total_height)` reserves a child panel's virtual height. `SetScrollY` clamps and updates its cursor immediately. `VisibleRows(count, row_height)` returns the half-open row range intersecting the current window clip, relative to the current cursor. It does not emit or advance rows. Set the cursor for each requested row and keep the reserved content height independent of how many rows were drawn.

The table contract test compares a 100-row and 100,000-row view with the same viewport, asserting the same submitted row count and geometry. It also prints a 1,000-frame headless timing; that timing excludes GPU work, font rasterization, data filtering and sorting. It is not an FPS benchmark or a comparison against other toolkits.

No network access or file writes are performed by the library. The renderer uses classic Direct3D 9, not 9Ex. Docking, multiple native viewports, gamepad navigation and an accessibility adapter are outside this version's scope.

## Verification

CTest runs input, editing, focus, popup, nested scrolling, geometry and virtualized table contracts without a graphics device. The demo's `--snapshot <file.png>` mode exercises real window messages for editing, undo/redo, combo selection, filtering, sorting, column resizing and navigation to the last record. It checks restored render state, resets the device and saves the rendered result. `--snapshot-menu <file.png>` leaves the menu open; `--snapshot-dark <file.png>` renders the dark palette. Snapshot modes require a working local D3D9 adapter and fail if a lost device cannot recover within three seconds. CI runs Debug/Release contracts and builds a separate consumer from the installed package on x86 and x64.
