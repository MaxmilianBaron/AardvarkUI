# Integration

## CMake

Use `add_subdirectory(AardvarkUI)` and link `AardvarkUI::AardvarkUI`, or install a built configuration:

```powershell
cmake --install build --config Release --prefix package
```

Consumers use `find_package(AardvarkUI 0.2 CONFIG REQUIRED)` with the installation in `CMAKE_PREFIX_PATH`. Match the target architecture and C++ runtime configuration. The static target carries its Direct3D 9, GDI and User32 dependencies.

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

Focus loss and cancelled mouse capture clear pressed state. `SetInputEnabled(false)`, `NoInputs` and `BeginDisabled` prevent interaction. Tab/Shift+Tab traverse controls; Enter/Space activate them. Sliders accept arrows and Home/End. A closed combo accepts Up/Down; an open combo supports Tab and Enter. `SetKeyboardFocusHere()` focuses the next enabled control. Wheel events go to the deepest hovered scrollable panel, then its parent when the child reaches its limit.

`InputText` edits a caller-owned, single-line UTF-8 `std::string`. Supply valid single-line UTF-8 without embedded NUL characters. The byte limit caps insertions without splitting a code point; lowering it does not silently truncate existing content. Fields or limits over 1 MiB are rejected.

Text input supports selection, dragging, Home/End, Ctrl+arrows, Ctrl+A/C/X/V and Ctrl+Z/Y/Shift+Z. Undo/redo history is bounded and discarded when the field stops being submitted. `TextReadOnly` prevents edits; `TextPassword` masks display and disables copying/cutting. It is not secure storage and does not erase the caller's string. `TextEnterReturnsTrue` changes the return value from "edited" to "submitted".

Default clipboard access uses Win32 Unicode text. `SetClipboardHandlers` replaces both operations; callbacks and their user pointer must remain valid while installed. UTF-16 character messages are converted to UTF-8. Navigation operates on code points, not grapheme clusters. IME composition UI, bidirectional layout and complex-script shaping are not implemented.

## Layout and style

Widgets advance the cursor vertically. `SameLine`, `Spacing`, `Separator`, `Dummy` and `SetCursorScreenPos` support custom layouts. `BeginTable` creates equal-width columns; call `TableNextColumn` before each cell. It does not sort data or resize columns. Use `BeginChild` for scrolling.

Labels and IDs must be stable and null-terminated. Scope repeated labels with `PushID`/`PopID`. Tables scope cell IDs automatically. Submit windows and controls once per frame in a stable order. `OpenPopup` opens one popup at the last item's edge; clicking outside or pressing Escape closes it. Nesting popups is not supported.

`GetStyle()` exposes colors, font size, spacing and rounding. The default is a light palette with dark buttons and green accents. `Text` uses the style text color when its color argument is zero. The demonstration icon is embedded only in the example executable; it is not a runtime library dependency.

No network access or file writes are performed by the library. The renderer uses classic Direct3D 9, not 9Ex. Docking, multiple native viewports, gamepad navigation and an accessibility adapter are outside this version's scope.

## Verification

CTest runs input, editing, focus, popup, nested scrolling and geometry contracts without a graphics device. The demo's `--snapshot <file.png>` mode exercises real window messages for editing, undo/redo, a combo selection and a button click, checks restored render state, resets the device and saves the rendered result. `--snapshot-menu <file.png>` leaves the menu open for visual inspection. Both require a working local D3D9 adapter; CI runs the headless contracts.
