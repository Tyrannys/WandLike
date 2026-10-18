#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "app/gfx.h"

#include <dwmapi.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <stb/stb_image.h>

#include <vector>

#include "hx/version.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace hx::ui {

namespace {
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGIFactory* g_factory = nullptr;
std::map<std::string, Texture> g_textures;
float g_dpi = 1.0f;
const wchar_t* kClass = L"HexwrightWindow";
bool g_class_registered = false;

LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* w = reinterpret_cast<UiWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (w) return w->handle_message(h, msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}
}  // namespace

namespace gfx {

bool init() {
    if (g_device) return true;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_dpi = (float)GetDpiForSystem() / 96.0f;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2, D3D11_SDK_VERSION, &g_device, &got, &g_context);
    if (FAILED(hr))   // fall back to the software rasteriser (remote desktop / broken drivers)
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2, D3D11_SDK_VERSION, &g_device, &got, &g_context);
    if (FAILED(hr)) return false;
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    g_device->QueryInterface(IID_PPV_ARGS(&dxgi));
    dxgi->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&g_factory));
    adapter->Release();
    dxgi->Release();
    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kClass;
    g_class_registered = RegisterClassExW(&wc) != 0;
    return g_class_registered;
}

void shutdown() {
    for (auto& [k, t] : g_textures) if (t.srv) t.srv->Release();
    g_textures.clear();
    if (g_factory) { g_factory->Release(); g_factory = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

ID3D11Device* device() { return g_device; }
ID3D11DeviceContext* context() { return g_context; }
float dpi_scale() { return g_dpi; }

const Texture* texture(const fs::path& file) {
    if (file.empty()) return nullptr;
    std::string key = narrow(file.wstring());
    auto it = g_textures.find(key);
    if (it != g_textures.end()) return it->second.srv ? &it->second : nullptr;
    Texture t;
    if (auto data = read_file(file, 32u << 20)) {
        int w = 0, h = 0, n = 0;
        unsigned char* px = stbi_load_from_memory((const unsigned char*)data->data(), (int)data->size(), &w, &h, &n, 4);
        if (px) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd{px, (UINT)(w * 4), 0};
            ID3D11Texture2D* tex = nullptr;
            if (SUCCEEDED(g_device->CreateTexture2D(&td, &sd, &tex))) {
                g_device->CreateShaderResourceView(tex, nullptr, &t.srv);
                tex->Release();
                t.w = w;
                t.h = h;
            }
            stbi_image_free(px);
        }
    }
    g_textures[key] = t;
    return t.srv ? &g_textures[key] : nullptr;
}

}  // namespace gfx

std::unique_ptr<UiWindow> UiWindow::create(const WindowDesc& d) {
    std::unique_ptr<UiWindow> w(new UiWindow());
    if (!w->setup(d)) return nullptr;
    return w;
}

static void load_fonts(ImGuiIO& io, Fonts& f, float scale) {
    ImFontGlyphRangesBuilder b;
    b.AddRanges(io.Fonts->GetGlyphRangesDefault());
    b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    b.AddRanges(io.Fonts->GetGlyphRangesGreek());
    b.AddText("\xE2\x80\xA2\xE2\x80\xA6\xE2\x86\x92\xE2\x9C\x93\xC2\xB7\xE2\x80\x94");   // • … → ✓ · —
    static ImVector<ImWchar> ranges;
    ranges.clear();
    b.BuildRanges(&ranges);
    auto load = [&](const char* file, float px) -> ImFont* {
        std::string path = std::string("C:\\Windows\\Fonts\\") + file;
        if (!fs::exists(path)) return nullptr;
        return io.Fonts->AddFontFromFileTTF(path.c_str(), px * scale, nullptr, ranges.Data);
    };
    f.regular = load("segoeui.ttf", 15.0f);
    if (!f.regular) f.regular = io.Fonts->AddFontDefault();
    f.bold = load("seguisb.ttf", 15.0f);
    f.large = load("seguisb.ttf", 24.0f);
    f.huge = load("seguisb.ttf", 34.0f);
    f.mono = load("consola.ttf", 14.0f);
    if (!f.bold) f.bold = f.regular;
    if (!f.large) f.large = f.regular;
    if (!f.huge) f.huge = f.large;
    if (!f.mono) f.mono = f.regular;
    io.FontDefault = f.regular;
}

bool UiWindow::setup(const WindowDesc& d) {
    overlay_ = d.overlay;
    min_w_ = d.min_width;
    min_h_ = d.min_height;
    float s = gfx::dpi_scale();
    DWORD style = d.overlay ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    DWORD ex = d.overlay ? (WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED) : 0;
    RECT r{0, 0, (LONG)(d.width * s), (LONG)(d.height * s)};
    AdjustWindowRectExForDpi(&r, style, FALSE, ex, GetDpiForSystem());
    hwnd_ = CreateWindowExW(ex, kClass, d.title.c_str(), style, d.x, d.y, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    if (d.overlay) SetLayeredWindowAttributes(hwnd_, 0, 238, LWA_ALPHA);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    if (FAILED(g_factory_create(sd))) return false;
    create_target();

    IMGUI_CHECKVERSION();
    ctx_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;                          // no imgui.ini next to the exe
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    load_fonts(io, fonts_, s);
    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(gfx::device(), gfx::context());
    if (d.visible) ShowWindow(hwnd_, d.overlay ? SW_SHOWNOACTIVATE : SW_SHOW);
    return true;
}

HRESULT UiWindow::g_factory_create(DXGI_SWAP_CHAIN_DESC& sd) {
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory* factory = nullptr;
    gfx::device()->QueryInterface(IID_PPV_ARGS(&dxgi));
    dxgi->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&factory));
    HRESULT hr = factory->CreateSwapChain(gfx::device(), &sd, &swap_);
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    factory->Release();
    adapter->Release();
    dxgi->Release();
    return hr;
}

UiWindow::~UiWindow() {
    if (ctx_) {
        ImGui::SetCurrentContext(ctx_);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(ctx_);
    }
    release_target();
    if (swap_) swap_->Release();
    if (hwnd_ && IsWindow(hwnd_)) { SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0); DestroyWindow(hwnd_); }
}

void UiWindow::create_target() {
    ID3D11Texture2D* back = nullptr;
    swap_->GetBuffer(0, IID_PPV_ARGS(&back));
    gfx::device()->CreateRenderTargetView(back, nullptr, &rtv_);
    back->Release();
}

void UiWindow::release_target() {
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
}

void UiWindow::show(bool v) {
    ShowWindow(hwnd_, v ? (overlay_ ? SW_SHOWNOACTIVATE : SW_SHOW) : SW_HIDE);
}

bool UiWindow::visible() const { return IsWindowVisible(hwnd_) != FALSE; }

LRESULT UiWindow::handle_message(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (ctx_) {
        ImGui::SetCurrentContext(ctx_);
        if (ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp)) return true;
    }
    switch (msg) {
        case WM_SIZE:
            minimized_ = wp == SIZE_MINIMIZED;
            if (wp != SIZE_MINIMIZED) { resize_w_ = LOWORD(lp); resize_h_ = HIWORD(lp); }
            return 0;
        case WM_GETMINMAXINFO:
            if (min_w_ > 0) {
                auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
                mm->ptMinTrackSize.x = (LONG)(min_w_ * gfx::dpi_scale());
                mm->ptMinTrackSize.y = (LONG)(min_h_ * gfx::dpi_scale());
            }
            return 0;
        case WM_MOUSEACTIVATE:
            if (overlay_) return MA_NOACTIVATE;                     // the overlay never steals focus from the game
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_KEYMENU) return 0;               // no Alt-key menu beep
            break;
        case WM_DPICHANGED: {
            auto* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_CLOSE:
            if (on_close_ && !on_close_()) return 0;
            closed_ = true;
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            closed_ = true;
            if (!overlay_) PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool UiWindow::frame(const std::function<void()>& gui) {
    if (closed_ || !IsWindow(hwnd_)) return false;
    if (minimized_ || !IsWindowVisible(hwnd_)) { Sleep(30); return true; }
    if (resize_w_ && resize_h_) {
        release_target();
        swap_->ResizeBuffers(0, resize_w_, resize_h_, DXGI_FORMAT_UNKNOWN, 0);
        create_target();
        resize_w_ = resize_h_ = 0;
    }
    ImGui::SetCurrentContext(ctx_);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    gui();
    ImGui::Render();
    const float clear[4] = {0.043f, 0.047f, 0.063f, 1.0f};
    gfx::context()->OMSetRenderTargets(1, &rtv_, nullptr);
    gfx::context()->ClearRenderTargetView(rtv_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swap_->Present(1, 0);
    return !closed_;
}

bool pump_messages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

}  // namespace hx::ui
