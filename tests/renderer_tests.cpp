#include <aardvark/ui.hpp>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace ui = aardvark::ui;
static unsigned checks = 0, failures = 0;
#define CHECK(value)                                                                                         \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(value)) {                                                                                      \
            ++failures;                                                                                      \
            std::fprintf(stderr, "Line %d: %s\n", __LINE__, #value);                                         \
        }                                                                                                    \
    } while (false)
#define REQUIRE(value)                                                                                       \
    do {                                                                                                     \
        if (!(value)) {                                                                                      \
            std::fprintf(stderr, "Setup %d: %s\n", __LINE__, #value);                                        \
            return 2;                                                                                        \
        }                                                                                                    \
    } while (false)

int main() {
    WNDCLASSW window_class{};
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.lpszClassName = L"AardvarkRenderTest";
    REQUIRE(RegisterClassW(&window_class));
    HWND window = CreateWindowW(window_class.lpszClassName, L"Renderer contracts", WS_OVERLAPPEDWINDOW, 0, 0,
                                320, 240, nullptr, nullptr, window_class.hInstance, nullptr);
    REQUIRE(window != nullptr);
    auto *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    REQUIRE(d3d != nullptr);
    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.BackBufferWidth = 192;
    parameters.BackBufferHeight = 128;
    IDirect3DDevice9 *device = nullptr;
    REQUIRE(SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &device)));
    D3DCAPS9 caps{};
    REQUIRE(SUCCEEDED(device->GetDeviceCaps(&caps)));
    REQUIRE(caps.PrimitiveMiscCaps & D3DPMISCCAPS_SEPARATEALPHABLEND);
    IDirect3DSurface9 *original = nullptr, *target = nullptr, *readback = nullptr;
    REQUIRE(SUCCEEDED(device->GetRenderTarget(0, &original)));
    REQUIRE(SUCCEEDED(device->CreateRenderTarget(192, 128, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                 &target, nullptr)));
    REQUIRE(SUCCEEDED(device->CreateOffscreenPlainSurface(192, 128, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                          &readback, nullptr)));
    REQUIRE(SUCCEEDED(device->SetRenderTarget(0, target)));
    IDirect3DVertexBuffer9 *host_stream = nullptr;
    IDirect3DTexture9 *host_texture = nullptr;
    REQUIRE(SUCCEEDED(device->CreateVertexBuffer(256, 0, 0, D3DPOOL_MANAGED, &host_stream, nullptr)));
    REQUIRE(SUCCEEDED(
        device->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &host_texture, nullptr)));
    D3DLOCKED_RECT texel{};
    REQUIRE(SUCCEEDED(host_texture->LockRect(0, &texel, nullptr, 0)));
    *static_cast<DWORD *>(texel.pBits) = 0xffffffffu;
    REQUIRE(SUCCEEDED(host_texture->UnlockRect(0)));
    auto *context = ui::CreateContext(window, device);
    ui::SetCurrentContext(context);
    REQUIRE(ui::LoadFont(L"Cascadia Code", 24));
    const std::pair<D3DRENDERSTATETYPE, DWORD> render_states[] = {
        {D3DRS_ZENABLE, TRUE},
        {D3DRS_ZWRITEENABLE, TRUE},
        {D3DRS_LIGHTING, TRUE},
        {D3DRS_CULLMODE, D3DCULL_CW},
        {D3DRS_FILLMODE, D3DFILL_WIREFRAME},
        {D3DRS_SHADEMODE, D3DSHADE_FLAT},
        {D3DRS_WRAP0, D3DWRAP_U},
        {D3DRS_ALPHATESTENABLE, TRUE},
        {D3DRS_STENCILENABLE, TRUE},
        {D3DRS_FOGENABLE, TRUE},
        {D3DRS_ALPHABLENDENABLE, FALSE},
        {D3DRS_SRCBLEND, D3DBLEND_ZERO},
        {D3DRS_DESTBLEND, D3DBLEND_ONE},
        {D3DRS_BLENDOP, D3DBLENDOP_SUBTRACT},
        {D3DRS_SEPARATEALPHABLENDENABLE, TRUE},
        {D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO},
        {D3DRS_DESTBLENDALPHA, D3DBLEND_ONE},
        {D3DRS_BLENDOPALPHA, D3DBLENDOP_REVSUBTRACT},
        {D3DRS_SCISSORTESTENABLE, FALSE},
        {D3DRS_COLORWRITEENABLE, 1},
        {D3DRS_SRGBWRITEENABLE, TRUE},
        {D3DRS_MULTISAMPLEMASK, 0}};
    const std::pair<D3DSAMPLERSTATETYPE, DWORD> sampler_states[] = {
        {D3DSAMP_MINFILTER, D3DTEXF_POINT},     {D3DSAMP_MAGFILTER, D3DTEXF_POINT},
        {D3DSAMP_MIPFILTER, D3DTEXF_LINEAR},    {D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP},
        {D3DSAMP_ADDRESSV, D3DTADDRESS_MIRROR}, {D3DSAMP_SRGBTEXTURE, TRUE}};
    const std::pair<D3DTEXTURESTAGESTATETYPE, DWORD> texture_states[] = {
        {D3DTSS_TEXCOORDINDEX, 1},         {D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2},
        {D3DTSS_COLORARG1, D3DTA_DIFFUSE}, {D3DTSS_COLORARG2, D3DTA_TEXTURE},
        {D3DTSS_ALPHAARG1, D3DTA_DIFFUSE}, {D3DTSS_ALPHAARG2, D3DTA_TEXTURE},
        {D3DTSS_COLOROP, D3DTOP_ADD},      {D3DTSS_ALPHAOP, D3DTOP_ADD}};
    const D3DVIEWPORT9 host_viewport{23, 17, 100, 70, 0.2f, 0.8f};
    const RECT host_scissor{31, 29, 59, 49};
    const DWORD host_fvf = D3DFVF_XYZ | D3DFVF_NORMAL;
    for (int mode = 0; mode < 3; ++mode) {
        for (auto setting : render_states)
            REQUIRE(SUCCEEDED(device->SetRenderState(setting.first, setting.second)));
        for (auto setting : sampler_states)
            REQUIRE(SUCCEEDED(device->SetSamplerState(0, setting.first, setting.second)));
        for (auto setting : texture_states)
            REQUIRE(SUCCEEDED(device->SetTextureStageState(0, setting.first, setting.second)));
        REQUIRE(SUCCEEDED(device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_ADD)));
        REQUIRE(SUCCEEDED(device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_ADD)));
        REQUIRE(SUCCEEDED(device->SetFVF(host_fvf)));
        REQUIRE(SUCCEEDED(device->SetTexture(0, host_texture)));
        REQUIRE(SUCCEEDED(device->SetStreamSource(0, host_stream, 16, 24)));
        REQUIRE(SUCCEEDED(device->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 3)));
        REQUIRE(SUCCEEDED(device->SetViewport(&host_viewport)));
        REQUIRE(SUCCEEDED(device->SetScissorRect(&host_scissor)));
        REQUIRE(
            SUCCEEDED(device->SetTextureStageState(0, D3DTSS_RESULTARG, mode ? D3DTA_TEMP : D3DTA_CURRENT)));
        const D3DVIEWPORT9 full_viewport{0, 0, 192, 128, 0, 1};
        REQUIRE(SUCCEEDED(device->SetViewport(&full_viewport)));
        REQUIRE(SUCCEEDED(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0)));
        ui::NewFrame();
        ui::SetNextWindowPos({0, 0});
        ui::SetNextWindowSize({192, 128});
        ui::Begin("Renderer");
        ui::GetWindowDrawList()->AddRectFilled({10, 10}, {60, 60}, ui::RGBA(200, 100, 50, 128));
        auto *draw = ui::GetWindowDrawList();
        draw->PushClipRect({125, 10}, {145, 60});
        draw->AddImage(host_texture, {120, 10}, {150, 60}, {}, {1, 1}, ui::RGBA(0, 255, 0, 128));
        draw->PopClipRect();
        draw->AddRectFilled({70, 10}, {110, 60}, ui::RGBA(0, 0, 255, 128));
        ui::GetForegroundDrawList()->AddRectFilled({70, 10}, {110, 60}, ui::RGBA(255, 0, 0, 128));
        draw->AddText(ui::GetFont(), 18, {10, 85}, ui::White, "Renderer / UTF-8");
        const auto invalid = draw->commands.front();
        auto bad_clip = invalid;
        bad_clip.clip.min.x = std::numeric_limits<float>::quiet_NaN();
        draw->commands.push_back(bad_clip);
        bad_clip.clip.min.x = std::numeric_limits<float>::infinity();
        draw->commands.push_back(bad_clip);
        auto bad_range = invalid;
        bad_range.first = std::numeric_limits<std::size_t>::max();
        draw->commands.push_back(bad_range);
        bad_range = invalid;
        bad_range.count = std::numeric_limits<std::size_t>::max();
        draw->commands.push_back(bad_range);
        bad_range.count = 5;
        draw->commands.push_back(bad_range);
        ui::End();
        REQUIRE(SUCCEEDED(device->SetViewport(&host_viewport)));
        REQUIRE(SUCCEEDED(device->BeginScene()));
        ui::Render();
        REQUIRE(SUCCEEDED(device->EndScene()));
        DWORD result = 0;
        CHECK(SUCCEEDED(device->GetTextureStageState(0, D3DTSS_RESULTARG, &result)));
        CHECK(result == static_cast<DWORD>(mode ? D3DTA_TEMP : D3DTA_CURRENT));
        for (auto setting : render_states) {
            CHECK(SUCCEEDED(device->GetRenderState(setting.first, &result)));
            CHECK(result == setting.second);
        }
        for (auto setting : sampler_states) {
            CHECK(SUCCEEDED(device->GetSamplerState(0, setting.first, &result)));
            CHECK(result == setting.second);
        }
        for (auto setting : texture_states) {
            CHECK(SUCCEEDED(device->GetTextureStageState(0, setting.first, &result)));
            CHECK(result == setting.second);
        }
        for (auto stage : {D3DTSS_COLOROP, D3DTSS_ALPHAOP}) {
            CHECK(SUCCEEDED(device->GetTextureStageState(1, stage, &result)));
            CHECK(result == D3DTOP_ADD);
        }
        D3DVIEWPORT9 restored_viewport{};
        CHECK(SUCCEEDED(device->GetViewport(&restored_viewport)));
        CHECK(std::memcmp(&restored_viewport, &host_viewport, sizeof(host_viewport)) == 0);
        RECT restored_scissor{};
        CHECK(SUCCEEDED(device->GetScissorRect(&restored_scissor)));
        CHECK(EqualRect(&restored_scissor, &host_scissor));
        CHECK(SUCCEEDED(device->GetFVF(&result)));
        CHECK(result == host_fvf);
        IDirect3DBaseTexture9 *restored_texture = nullptr;
        CHECK(SUCCEEDED(device->GetTexture(0, &restored_texture)));
        CHECK(restored_texture == host_texture);
        if (restored_texture)
            restored_texture->Release();
        IDirect3DVertexBuffer9 *restored_stream = nullptr;
        UINT restored_offset = 0, restored_stride = 0, restored_frequency = 0;
        CHECK(SUCCEEDED(device->GetStreamSource(0, &restored_stream, &restored_offset, &restored_stride)));
        CHECK(restored_stream == host_stream && restored_offset == 16 && restored_stride == 24);
        if (restored_stream)
            restored_stream->Release();
        CHECK(SUCCEEDED(device->GetStreamSourceFreq(0, &restored_frequency)));
        CHECK(restored_frequency == (D3DSTREAMSOURCE_INDEXEDDATA | 3));
        CHECK(draw->commands.empty() && draw->vertices.empty());
        CHECK(ui::GetForegroundDrawList()->commands.empty());
        REQUIRE(SUCCEEDED(device->GetRenderTargetData(target, readback)));
        D3DLOCKED_RECT pixels{};
        REQUIRE(SUCCEEDED(readback->LockRect(&pixels, nullptr, D3DLOCK_READONLY)));
        const auto pixel = [&](unsigned x, unsigned y) {
            return *reinterpret_cast<const DWORD *>(static_cast<const char *>(pixels.pBits) +
                                                    pixels.Pitch * y + x * 4);
        };
        const auto color = pixel(30, 30);
        const auto close_channel = [](unsigned first, unsigned second) {
            return std::abs(static_cast<int>(first) - static_cast<int>(second)) <= 1;
        };
        CHECK(close_channel((color >> 24) & 255, 128));
        CHECK(close_channel((color >> 16) & 255, 100));
        CHECK(close_channel((color >> 8) & 255, 50));
        CHECK(close_channel(color & 255, 25));
        const auto layered = pixel(90, 30);
        CHECK(close_channel((layered >> 24) & 255, 192));
        CHECK(close_channel((layered >> 16) & 255, 128));
        CHECK(((layered >> 8) & 255) == 0);
        CHECK(close_channel(layered & 255, 64));
        const auto textured = pixel(135, 30);
        CHECK(close_channel((textured >> 24) & 255, 128));
        CHECK(close_channel((textured >> 8) & 255, 128));
        CHECK(pixel(121, 30) == 0 && pixel(148, 30) == 0);
        bool text_visible = false;
        for (unsigned y = 85; y < 120; ++y)
            for (unsigned x = 10; x < 180; ++x)
                text_visible |= pixel(x, y) != 0;
        CHECK(text_visible);
        REQUIRE(SUCCEEDED(readback->UnlockRect()));
        if (mode == 1) {
            REQUIRE(SUCCEEDED(device->SetRenderTarget(0, original)));
            target->Release();
            original->Release();
            REQUIRE(SUCCEEDED(device->Reset(&parameters)));
            REQUIRE(SUCCEEDED(device->GetRenderTarget(0, &original)));
            REQUIRE(SUCCEEDED(device->CreateRenderTarget(192, 128, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0,
                                                         FALSE, &target, nullptr)));
            REQUIRE(SUCCEEDED(device->SetRenderTarget(0, target)));
        }
    }
    ui::DestroyContext(context);
    device->SetTexture(0, nullptr);
    device->SetStreamSource(0, nullptr, 0, 0);
    host_stream->Release();
    host_texture->Release();
    device->SetRenderTarget(0, original);
    target->Release();
    readback->Release();
    original->Release();
    CHECK(SUCCEEDED(device->Reset(&parameters)));
    device->Release();
    d3d->Release();
    DestroyWindow(window);
    UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
    std::printf("%u renderer checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
