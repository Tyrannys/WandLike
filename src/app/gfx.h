#pragma once
// Direct3D 11 + Win32 windows hosting Dear ImGui. One shared device; each window has its own swap chain and ImGui context.
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

#include <imgui.h>

#include "core/util.h"

namespace hx::ui {

struct Texture {
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0;
};

// Shared D3D11 device + image loading. Call init() once before creating windows.
namespace gfx {
bool init();
void shutdown();
ID3D11Device* device();
ID3D11DeviceContext* context();
const Texture* texture(const fs::path& image_file);     // decoded with stb_image, cached; nullptr if missing / unreadable
float dpi_scale();                                      // system DPI / 96
}  // namespace gfx

struct Fonts {
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;
    ImFont* large = nullptr;                            // page titles
    ImFont* huge = nullptr;                             // hero titles
    ImFont* mono = nullptr;
};

struct WindowDesc {
    std::wstring title;
    int width = 1180, height = 780;
    int min_width = 0, min_height = 0;
    bool overlay = false;                               // borderless, topmost, translucent, never takes focus
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    bool visible = true;
};

class UiWindow {
public:
    static std::unique_ptr<UiWindow> create(const WindowDesc& d);
    ~UiWindow();
    UiWindow(const UiWindow&) = delete;
    UiWindow& operator=(const UiWindow&) = delete;

    // Runs one frame: the callback issues ImGui calls. Returns false once the window was closed.
    bool frame(const std::function<void()>& gui);
    [[nodiscard]] HWND hwnd() const { return hwnd_; }
    [[nodiscard]] const Fonts& fonts() const { return fonts_; }
    [[nodiscard]] bool minimized() const { return minimized_; }
    void show(bool visible);
    [[nodiscard]] bool visible() const;
    void close_requested(std::function<bool()> handler) { on_close_ = std::move(handler); }   // return true to allow closing
    void set_title(const std::wstring& t) { SetWindowTextW(hwnd_, t.c_str()); }

    ImGuiContext* context() const { return ctx_; }
    // Messages are dispatched here from the shared window procedure.
    LRESULT handle_message(HWND h, UINT msg, WPARAM wp, LPARAM lp);

private:
    UiWindow() = default;
    bool setup(const WindowDesc& d);
    HRESULT g_factory_create(DXGI_SWAP_CHAIN_DESC& sd);
    void create_target();
    void release_target();

    HWND hwnd_ = nullptr;
    ImGuiContext* ctx_ = nullptr;
    IDXGISwapChain* swap_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    Fonts fonts_;
    bool minimized_ = false;
    bool closed_ = false;
    bool overlay_ = false;
    UINT resize_w_ = 0, resize_h_ = 0;
    int min_w_ = 0, min_h_ = 0;
    std::function<bool()> on_close_;
};

// Pumps the Win32 message queue; false when WM_QUIT was received.
bool pump_messages();

}  // namespace hx::ui
