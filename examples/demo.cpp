#include <aardvark/ui.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <gdiplus.h>
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

static LRESULT CALLBACK window_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (context)
        ui::Message(window, message, wparam, lparam);
    if (message == WM_SIZE && wparam != SIZE_MINIMIZED)
        resize_pending = true;
    if (message == WM_GETMINMAXINFO) {
        auto *limits = reinterpret_cast<MINMAXINFO *>(lparam);
        limits->ptMinTrackSize = {800, 640};
        return 0;
    }
    if (message == WM_DESTROY) {
        running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void label(float x, float y, const char *text, float size = 18,
                  ui::Color color = ui::RGBA(218, 225, 235)) {
    ui::SetCursorScreenPos({x, y});
    ui::Text(text, size, color);
}

static void contents(bool snapshot) {
    const auto screen = ui::GetIO().DisplaySize;
    ui::SetNextWindowPos({0, 0});
    ui::SetNextWindowSize(screen);
    ui::Begin("Demo");
    auto *draw = ui::GetWindowDrawList();
    draw->AddRectFilled({24, 24}, {screen.x - 24, 108}, ui::RGBA(24, 31, 42));
    draw->AddRectFilled({24, 24}, {28, 108}, ui::RGBA(101, 188, 246));
    label(46, 36, "AardvarkUI", 32, ui::White);
    label(47, 76, "A small immediate-mode UI for Windows", 16, ui::RGBA(150, 167, 189));
    label(screen.x - 108, 50, "v0.1.0", 17, ui::RGBA(113, 189, 255));
    draw->AddRectFilled({24, 128}, {384, screen.y - 62}, ui::RGBA(24, 31, 42));
    draw->AddRectFilled({404, 128}, {screen.x - 24, screen.y - 62}, ui::RGBA(24, 31, 42));
    label(46, 150, "Controls", 23, ui::White);
    label(46, 185, "Click, drag and scroll.", 17, ui::RGBA(150, 167, 189));
    ui::SetCursorScreenPos({46, 231});
    if (ui::Button("Add a click", {316, 42}))
        ++clicks;
    char counter[48]{};
    std::snprintf(counter, sizeof(counter), "Button clicks: %d", clicks);
    label(46, 291, counter);
    ui::SetCursorScreenPos({46, 343});
    ui::Checkbox("Show chart", chart);
    ui::SetCursorScreenPos({46, 405});
    ui::SliderFloat("Amplitude", amplitude, 0, 1, 316);
    label(46, 486, "No external fonts or textures.", 16, ui::RGBA(150, 167, 189));
    label(46, 512, "Resize the window to try the layout.", 16, ui::RGBA(150, 167, 189));
    label(428, 150, "Draw list", 23, ui::White);
    label(428, 185, "Clipped geometry and UTF-8 text", 17, ui::RGBA(150, 167, 189));
    const float width = screen.x - 476;
    const ui::Point top{428, 233}, bottom{screen.x - 48, 383};
    draw->AddRectFilled(top, bottom, ui::RGBA(15, 21, 30));
    draw->PushClipRect(top, bottom);
    for (int row = 1; row < 5; ++row)
        draw->AddLine({top.x, top.y + row * 30}, {bottom.x, top.y + row * 30}, ui::RGBA(33, 44, 59));
    if (chart) {
        const float phase = snapshot ? 0.7f : static_cast<float>(GetTickCount64() % 10000) / 1400;
        ui::Point previous{top.x, top.y + 75};
        for (int step = 0; step <= 160; ++step) {
            const float x = static_cast<float>(step) / 160;
            const ui::Point next{top.x + width * x, top.y + 75 - std::sin(x * 13 + phase) * amplitude * 56};
            if (step)
                draw->AddLine(previous, next, ui::RGBA(99, 193, 253), 2);
            previous = next;
        }
    }
    draw->PopClipRect();
    label(428, 409, "Scrollable child", 19, ui::White);
    label(428, 437, "20 entries / mouse wheel", 15, ui::RGBA(150, 167, 189));
    ui::SetCursorScreenPos({428, 467});
    draw->AddRectFilled({428, 467}, {screen.x - 48, screen.y - 83}, ui::RGBA(15, 21, 30));
    ui::BeginChild("Entries", {width, screen.y - 550});
    for (int row = 0; row < 20; ++row) {
        const auto position = ui::GetCursorScreenPos();
        char item[80]{};
        std::snprintf(item, sizeof(item),
                      "Item %02d  /  P\xc5\x99\xc3\xadli\xc5\xa1 \xc5\xbelu\xc5\xa5ou\xc4\x8dk\xc3\xbd",
                      row + 1);
        ui::GetWindowDrawList()->AddText(ui::GetFont(), 17, {position.x + 10, position.y + 6},
                                         ui::RGBA(185, 204, 225), item);
        ui::Dummy({width, 32});
    }
    ui::EndChild();
    label(24, screen.y - 39, "Direct3D 9  /  Win32  /  C++17", 15, ui::RGBA(123, 143, 169));
    label(screen.x - 191, screen.y - 39, "MIT licensed", 15, ui::RGBA(123, 143, 169));
    ui::End();
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

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    auto **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool snapshot = argv && argc == 3 && std::wstring(argv[1]) == L"--snapshot";
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
    RECT dimensions{0, 0, 960, 660};
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
    int result = ui::LoadFont() ? 0 : 14;
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
        if (snapshot && frames == 1)
            SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 250));
        if (snapshot && frames == 2)
            SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(100, 250));
        ui::NewFrame();
        contents(snapshot);
        device->Clear(0, nullptr, D3DCLEAR_TARGET, ui::RGBA(12, 17, 24), 1, 0);
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
        if (snapshot && frames == 3) {
            if (clicks != 1 || !save_image(path.c_str()))
                result = 4;
            break;
        }
        device->Present(nullptr, nullptr, nullptr, nullptr);
        ++frames;
        if (snapshot && frames == 3) {
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
    device->Release();
    device = nullptr;
    graphics->Release();
    if (IsWindow(window))
        DestroyWindow(window);
    UnregisterClassW(type.lpszClassName, instance);
    return result;
}
