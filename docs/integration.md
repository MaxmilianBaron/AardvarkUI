# Integration

Create a context after creating a Win32 window and a Direct3D 9 device. A context keeps a COM reference to the device; caller-owned image textures must remain valid until their draw commands have been flushed.

```cpp
#include <aardvark/ui.hpp>
namespace ui = aardvark::ui;

auto* context = ui::CreateContext(window, device);
ui::SetCurrentContext(context);
if (!ui::LoadFont(L"Segoe UI", 40)) {
    ui::DestroyContext(context);
    return;
}
```

Forward window messages with `ui::Message(window, message, wparam, lparam)`. Continue processing them in your window procedure. Focus loss or cancelled capture clears pressed state. Use `SetInputEnabled(false)` to turn off interaction.

Each frame, between your device's `BeginScene` and `EndScene`:

```cpp
ui::NewFrame();
ui::SetNextWindowPos({24, 24});
ui::SetNextWindowSize({260, 200});
ui::Begin("Tools");
ui::Text("Hello from AardvarkUI");
ui::Dummy({0, 12});
if (ui::Button("Apply")) {
    apply_changes();
}
ui::End();
ui::Render();
```

Windows are layout and clipping regions; draw their backgrounds with `GetWindowDrawList()->AddRectFilled(...)`. Positions, font sizes and extents are client pixels. The host application owns DPI policy. Set positions again after a DPI change.

Pair every `Begin`/`BeginChild` with `End`/`EndChild`. Widgets advance the vertical cursor. Use `SetCursorScreenPos`, `Dummy`, and `PushID`/`PopID` for custom layouts and repeated controls. Labels and IDs must be stable and null-terminated; bounded text can use `DrawList::AddText` with an end pointer from the same string.

`DrawList` supports filled rectangles, lines, textured quads and text. Clip rectangles nest. Consecutive commands with the same texture and clip are batched. `Flush` draws and clears the lists while restoring device state; `Render` also completes the input frame.

The font rasterizer uses GDI and lazily caches glyphs in managed textures. Text is UTF-8, including replacement of malformed sequences. Complex-script shaping and bidirectional layout are not implemented. Load one font per context; font size can be adjusted when drawing. Managed textures survive a normal Direct3D 9 `Reset`; recreate the context if replacing the device.

Use the context and device on one UI thread. The current context is explicit and is not thread-local. Destroy all contexts before the application's final device release:

```cpp
ui::DestroyContext(context);
```

No files are written and no network access is performed by the library. For input/geometry testing without a device, create an empty context and set `GetIO().DisplaySize` before `NewFrame`.

The demo's `--snapshot <file.png>` mode runs a real mouse click through the window procedure, checks restored render state, resets the device and saves the rendered result. It requires a working local Direct3D 9 adapter. CI runs the input and geometry contracts separately without requiring an adapter.
