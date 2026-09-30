# AardvarkUI

C++17 immediate-mode UI for **Win32 and Direct3D 9, x86 and x64**. MIT licensed.

UTF-8 editing, clipboard, undo, keyboard navigation, popups and scrolling. Virtualized data tables support row selection, sortable headers and resizable columns. Draw lists batch clipped geometry, images and text.

![AardvarkUI demo](docs/demo.png)

The demo searches and sorts 100,000 local records, with light/dark themes, Cascadia Code and an embedded Aardvarkland icon. Windows substitutes a font if needed. No external library dependencies.

Build with Visual Studio 2022 C++ tools, the Windows SDK and CMake 3.21 or newer:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\aardvark_ui_demo.exe
```

Use `-A Win32` in a separate directory for x86. CI checks Debug, Release and installed-package consumers on both architectures.

Add it to another CMake project:

```cmake
add_subdirectory(AardvarkUI)
target_link_libraries(your_app PRIVATE AardvarkUI::AardvarkUI)
```

See [integration](docs/integration.md) for installed packages, input routing and lifecycle. Classic D3D9 only; no docking, multi-viewport, accessibility adapter or complex-script shaping.
