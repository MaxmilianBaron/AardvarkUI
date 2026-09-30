# AardvarkUI

A small C++17 immediate-mode UI library for Win32 and Direct3D 9. It provides mouse-driven buttons, checkboxes, sliders, scrollable panels, UTF-8 text and clipped draw lists.

![AardvarkUI demo](docs/demo.png)

**Windows x86 and x64. MIT licensed.** Uses installed Windows fonts; no bundled fonts, textures or third-party libraries.

Build with Visual Studio 2022 C++ tools, the Windows SDK and CMake 3.21 or newer:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\aardvark_ui_demo.exe
```

Use `-A Win32` in a separate build directory for x86. The demo supports resizing and device reset.

Add it to another CMake project:

```cmake
add_subdirectory(AardvarkUI)
target_link_libraries(your_app PRIVATE AardvarkUI::AardvarkUI)
```

Or run `cmake --install build --config Release --prefix <directory>` and use `find_package(AardvarkUI CONFIG REQUIRED)`.

See the [integration guide](docs/integration.md) and [working example](examples/demo.cpp). This first version has mouse input only: no text-entry widgets, keyboard navigation, docking, accessibility adapter or text shaping. It uses classic Direct3D 9, not Direct3D 9Ex.

Bug reports, focused pull requests and small examples are welcome. Include the architecture and a minimal reproduction. See [LICENSE](LICENSE) for reuse terms.
