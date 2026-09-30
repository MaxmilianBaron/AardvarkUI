#include <aardvark/ui.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <gdiplus.h>
#include <objidl.h>
#include <shellapi.h>
#include <string>
#include <vector>

namespace ui = aardvark::ui;
static IDirect3DDevice9 *device = nullptr;
static D3DPRESENT_PARAMETERS presentation{};
static ui::Context *context = nullptr;
static bool running = true;
static bool resize_pending = false;
static int clicks = 0;
static bool chart = true;
static float amplitude = 0.65f;
static IDirect3DTexture9 *logo = nullptr;
static std::string workspace = "My workspace";
static int mode = 0, density = 0, selected_row = 0;
static bool advanced = false, diagnostics = false;

static LRESULT CALLBACK window_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (context)
        ui::Message(window, message, wparam, lparam);
    if (message == WM_SIZE && wparam != SIZE_MINIMIZED)
        resize_pending = true;
    if (message == WM_GETMINMAXINFO) {
        auto *limits = reinterpret_cast<MINMAXINFO *>(lparam);
        RECT minimum{0, 0, 1000, 800};
        AdjustWindowRectEx(&minimum, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
                           static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
        limits->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
        return 0;
    }
    if (message == WM_DESTROY) {
        running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void label(float x, float y, const char *text, float size = 17, ui::Color color = 0) {
    ui::SetCursorScreenPos({x, y});
    ui::Text(text, size, color ? color : ui::GetStyle().Text);
}

static void panel(ui::Point top, ui::Point bottom) {
    auto *draw = ui::GetWindowDrawList();
    draw->AddRectFilled({top.x, top.y + 2}, {bottom.x, bottom.y + 2}, ui::RGBA(228, 232, 236), 10);
    draw->AddRectFilled(top, bottom, ui::White, 10);
    draw->AddRect(top, bottom, ui::GetStyle().Border, 1, 10);
}

static void contents(bool snapshot) {
    const auto screen = ui::GetIO().DisplaySize;
    ui::SetNextWindowPos({0, 0});
    ui::SetNextWindowSize(screen);
    ui::Begin("Demo");
    auto *draw = ui::GetWindowDrawList();
    const auto &style = ui::GetStyle();
    draw->AddImage(logo, {30, 27}, {100, 97});
    label(122, 34, "AardvarkUI", 30);
    label(123, 77, "A compact toolkit for native interfaces.", 15, style.Muted);
    draw->AddRectFilled({screen.x - 154, 45}, {screen.x - 32, 77}, style.Selection, 16);
    draw->AddCircleFilled({screen.x - 136, 61}, 4, style.Accent);
    label(screen.x - 123, 53, "v0.2.0", 16, style.Accent);
    draw->AddLine({32, 119}, {screen.x - 32, 119}, style.Border);

    panel({32, 150}, {380, screen.y - 58});
    panel({404, 150}, {screen.x - 32, screen.y - 58});
    label(56, 174, "Controls", 22);
    label(56, 209, "Click, type or press Tab.", 14, style.Muted);
    ui::SetCursorScreenPos({56, 257});
    ui::InputText("Workspace name", workspace, 300, 128);
    ui::SetCursorScreenPos({56, 337});
    const char *modes[] = {"Balanced", "Responsive", "Quiet"};
    ui::Combo("Mode", mode, modes, 3, 300);
    ui::SetCursorScreenPos({56, 400});
    ui::Checkbox("Live preview", chart);
    ui::SetCursorScreenPos({56, 450});
    ui::SliderFloat("Intensity", amplitude, 0, 1, 300);
    ui::SetCursorScreenPos({56, 525});
    ui::RadioButton("Compact", density, 0);
    ui::SameLine(24);
    ui::RadioButton("Comfort", density, 1);
    ui::SetCursorScreenPos({56, 567});
    if (ui::CollapsingHeader("Options", advanced, 300)) {
        ui::SetCursorScreenPos({66, 612});
        ui::Checkbox("Show diagnostics", diagnostics);
    }
    ui::SetCursorScreenPos({56, 656});
    if (ui::Button("Apply changes", {186, 38}))
        ++clicks;
    ui::SameLine(12);
    if (ui::Button("Reset", {102, 38})) {
        workspace = "My workspace";
        mode = density = clicks = 0;
        chart = true;
        amplitude = 0.65f;
    }
    char saved[64]{};
    std::snprintf(saved, sizeof(saved), "%s / %d saved",
                  mode == 0   ? "Balanced"
                  : mode == 1 ? "Responsive"
                              : "Quiet",
                  clicks);
    label(56, 707, saved, 13, style.Muted);

    label(428, 174, "Live canvas", 22);
    label(428, 209, "Clipped geometry and UTF-8 text", 14, style.Muted);
    const ui::Point top{428, 253}, bottom{screen.x - 56, 413};
    const float width = bottom.x - top.x;
    draw->AddRectFilled(top, bottom, ui::RGBA(247, 249, 250), 6);
    draw->PushClipRect(top, bottom);
    for (int row = 1; row < 5; ++row)
        draw->AddLine({top.x, top.y + row * 32}, {bottom.x, top.y + row * 32}, ui::RGBA(230, 235, 238));
    if (chart) {
        const float phase = snapshot ? 0.7f : static_cast<float>(GetTickCount64() % 10000) / 1400;
        ui::Point previous{};
        for (int step = 0; step <= 180; ++step) {
            const float x = static_cast<float>(step) / 180;
            const ui::Point next{top.x + width * x, top.y + 80 - std::sin(x * 13 + phase) * amplitude * 65};
            if (step)
                draw->AddLine(previous, next, style.Accent, 2);
            previous = next;
        }
    }
    draw->PopClipRect();
    label(428, 443, "Data & selection", 20);
    label(428, 475, "20 entries / scroll to explore", 13, style.Muted);
    ui::SetCursorScreenPos({428, 510});
    ui::BeginChild("Records", {width, screen.y - 610});
    if (ui::BeginTable("records", 3, width)) {
        for (const char *heading : {"COMPONENT", "TYPE", "STATUS"}) {
            ui::TableNextColumn();
            ui::Text(heading, 12, style.Muted);
            ui::Spacing(10);
        }
        for (int row = 0; row < 20; ++row) {
            char entry[48]{};
            std::snprintf(entry, sizeof(entry), "Item %02d", row + 1);
            ui::TableNextColumn();
            if (ui::Selectable(entry, selected_row == row, {width / 3 - 16, density ? 40.0f : 32.0f}))
                selected_row = row;
            ui::TableNextColumn();
            ui::Spacing(8);
            ui::Text(row % 2 ? "Control" : "Surface", 14, style.Muted);
            ui::TableNextColumn();
            ui::Spacing(8);
            ui::Text("Ready", 14, style.Accent);
        }
        ui::EndTable();
    }
    ui::EndChild();
    char renderer[96]{};
    std::snprintf(renderer, sizeof(renderer), "%zu vertices / %zu batches", draw->vertices.size(),
                  draw->commands.size());
    label(428, screen.y - 85, diagnostics ? renderer : "Keyboard and mouse input enabled", 13, style.Muted);
    label(32, screen.y - 33, "C++17 / WIN32 / DIRECT3D 9", 12, style.Muted);
    label(screen.x - 235, screen.y - 33, "MIT / x86 + x64", 12, style.Muted);
    ui::End();
}

static bool load_logo(HINSTANCE instance) {
    const auto resource = FindResourceW(instance, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));
    if (!resource)
        return false;
    const auto bytes = SizeofResource(instance, resource);
    const auto loaded = LoadResource(instance, resource);
    auto *source = loaded ? LockResource(loaded) : nullptr;
    if (!source)
        return false;
    auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory)
        return false;
    auto *destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(destination, source, bytes);
    GlobalUnlock(memory);
    IStream *stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return false;
    }
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput options;
    bool success = Gdiplus::GdiplusStartup(&token, &options, nullptr) == Gdiplus::Ok;
    if (success) {
        {
            Gdiplus::Bitmap bitmap(stream);
            const auto width = bitmap.GetWidth(), height = bitmap.GetHeight();
            success = bitmap.GetLastStatus() == Gdiplus::Ok && width && height && width <= 2048 &&
                      height <= 2048 &&
                      SUCCEEDED(device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                                      &logo, nullptr));
            D3DLOCKED_RECT lock{};
            if (success) {
                success = SUCCEEDED(logo->LockRect(0, &lock, nullptr, 0));
                if (success) {
                    for (unsigned y = 0; y < height; ++y) {
                        auto *row = reinterpret_cast<std::uint32_t *>(
                            static_cast<unsigned char *>(lock.pBits) + y * lock.Pitch);
                        for (unsigned x = 0; x < width; ++x) {
                            Gdiplus::Color color;
                            if (bitmap.GetPixel(x, y, &color) != Gdiplus::Ok)
                                success = false;
                            row[x] = color.GetValue();
                        }
                    }
                    logo->UnlockRect(0);
                }
            }
        }
        Gdiplus::GdiplusShutdown(token);
    }
    stream->Release();
    return success;
}

static bool save_image(const wchar_t *path) {
    IDirect3DSurface9 *source = nullptr;
    IDirect3DSurface9 *copy = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &source)))
        return false;
    D3DSURFACE_DESC description{};
    source->GetDesc(&description);
    bool success = SUCCEEDED(device->CreateOffscreenPlainSurface(
        description.Width, description.Height, description.Format, D3DPOOL_SYSTEMMEM, &copy, nullptr));
    if (success)
        success = SUCCEEDED(device->GetRenderTargetData(source, copy));
    D3DLOCKED_RECT pixels{};
    if (success)
        success = SUCCEEDED(copy->LockRect(&pixels, nullptr, D3DLOCK_READONLY));
    if (success) {
        ULONG_PTR token = 0;
        Gdiplus::GdiplusStartupInput options;
        success = Gdiplus::GdiplusStartup(&token, &options, nullptr) == Gdiplus::Ok;
        if (success) {
            {
                UINT count = 0, bytes = 0;
                Gdiplus::GetImageEncodersSize(&count, &bytes);
                std::vector<unsigned char> storage(bytes);
                auto *codecs = reinterpret_cast<Gdiplus::ImageCodecInfo *>(storage.data());
                success = Gdiplus::GetImageEncoders(count, bytes, codecs) == Gdiplus::Ok;
                bool saved = false;
                for (UINT i = 0; success && i < count; ++i)
                    if (std::wstring(codecs[i].MimeType) == L"image/png") {
                        Gdiplus::Bitmap bitmap(description.Width, description.Height, pixels.Pitch,
                                               PixelFormat32bppRGB, static_cast<BYTE *>(pixels.pBits));
                        saved = bitmap.Save(path, &codecs[i].Clsid) == Gdiplus::Ok;
                        break;
                    }
                success = success && saved;
            }
            Gdiplus::GdiplusShutdown(token);
        }
        copy->UnlockRect();
    }
    if (copy)
        copy->Release();
    source->Release();
    return success;
}

static void snapshot_input(HWND window, int frame, bool menu) {
    const auto click = [&](int x, int y) {
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    };
    const auto control = [&](WPARAM key) {
        SendMessageW(window, WM_KEYDOWN, VK_CONTROL, 0);
        SendMessageW(window, WM_KEYDOWN, key, 0);
        SendMessageW(window, WM_KEYUP, key, 0);
        SendMessageW(window, WM_KEYUP, VK_CONTROL, 0);
    };
    if (frame == 1)
        click(130, 295);
    if (frame == 2) {
        control('A');
        for (const wchar_t letter : std::wstring(L"Aardvark workspace"))
            SendMessageW(window, WM_CHAR, letter, 0);
        control('Z');
        control('Y');
    }
    if (frame == 3 || (frame == 7 && menu))
        click(130, 355);
    if (frame == 4)
        click(130, 435);
    if (frame == 5)
        click(130, 580);
    if (frame == 6)
        click(130, 674);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    auto **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool menu = argv && argc == 3 && std::wstring(argv[1]) == L"--snapshot-menu";
    const bool snapshot = menu || (argv && argc == 3 && std::wstring(argv[1]) == L"--snapshot");
    const std::wstring path = snapshot ? argv[2] : L"";
    if (argv)
        LocalFree(argv);
    WNDCLASSEXW type{sizeof(type)};
    type.style = CS_CLASSDC | CS_DBLCLKS;
    type.lpfnWndProc = window_message;
    type.hInstance = instance;
    type.hCursor = LoadCursor(nullptr, IDC_ARROW);
    type.lpszClassName = L"AardvarkUIDemo";
    if (!RegisterClassExW(&type))
        return 10;
    RECT dimensions{0, 0, 1080, 800};
    AdjustWindowRect(&dimensions, WS_OVERLAPPEDWINDOW, FALSE);
    const auto window =
        CreateWindowW(type.lpszClassName, L"AardvarkUI", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                      dimensions.right - dimensions.left, dimensions.bottom - dimensions.top, nullptr,
                      nullptr, instance, nullptr);
    if (!window)
        return 11;
    auto *graphics = Direct3DCreate9(D3D_SDK_VERSION);
    if (!graphics) {
        DestroyWindow(window);
        return 12;
    }
    presentation.Windowed = TRUE;
    presentation.SwapEffect = D3DSWAPEFFECT_DISCARD;
    presentation.BackBufferFormat = D3DFMT_X8R8G8B8;
    presentation.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    presentation.hDeviceWindow = window;
    const auto created = graphics->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                                D3DCREATE_SOFTWARE_VERTEXPROCESSING, &presentation, &device);
    if (FAILED(created)) {
        std::fprintf(stderr, "CreateDevice failed: 0x%08lx\n", static_cast<unsigned long>(created));
        graphics->Release();
        DestroyWindow(window);
        return 13;
    }
    context = ui::CreateContext(window, device);
    ui::SetCurrentContext(context);
    int result = ui::LoadFont(L"Cascadia Code", 40) && load_logo(instance) ? 0 : 14;
    if (!snapshot)
        ShowWindow(window, show);
    resize_pending = false;
    int frames = 0;
    while (running && !result) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running)
            break;
        const auto cooperative = device->TestCooperativeLevel();
        if (cooperative == D3DERR_DEVICELOST) {
            if (snapshot) {
                result = 2;
                break;
            }
            Sleep(20);
            continue;
        }
        if (resize_pending || cooperative == D3DERR_DEVICENOTRESET) {
            presentation.BackBufferWidth = presentation.BackBufferHeight = 0;
            if (FAILED(device->Reset(&presentation))) {
                Sleep(20);
                continue;
            }
            resize_pending = false;
        }
        if (snapshot)
            snapshot_input(window, frames, menu);
        ui::NewFrame();
        contents(snapshot);
        device->Clear(0, nullptr, D3DCLEAR_TARGET, ui::RGBA(246, 247, 248), 1, 0);
        if (FAILED(device->BeginScene())) {
            result = 2;
            break;
        }
        device->SetRenderState(D3DRS_FOGENABLE, TRUE);
        ui::Render();
        DWORD fog = 0;
        device->GetRenderState(D3DRS_FOGENABLE, &fog);
        if (fog != TRUE)
            result = 3;
        device->EndScene();
        if (snapshot && frames == 8) {
            if (clicks != 1 || workspace != "Aardvark workspace" || mode != 1 || !advanced ||
                !save_image(path.c_str()))
                result = 4;
            break;
        }
        device->Present(nullptr, nullptr, nullptr, nullptr);
        ++frames;
        if (snapshot && frames == 8) {
            presentation.BackBufferWidth = presentation.BackBufferHeight = 0;
            if (FAILED(device->Reset(&presentation))) {
                result = 5;
                break;
            }
        }
        if (IsIconic(window))
            Sleep(20);
    }
    ui::DestroyContext(context);
    context = nullptr;
    if (logo)
        logo->Release();
    logo = nullptr;
    device->Release();
    device = nullptr;
    graphics->Release();
    if (IsWindow(window))
        DestroyWindow(window);
    UnregisterClassW(type.lpszClassName, instance);
    return result;
}
