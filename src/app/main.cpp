#include <windows.h>

#include <shellapi.h>

#include "app/gfx.h"
#include "app/state.h"
#include "app/theme.h"
#include "app/ui.h"
#include "core/util.h"
#include "hx/version.h"

namespace {

int run(bool debug, bool start_minimized) {
    using namespace hx;
    if (debug) {
        set_log_to_console(true);
        set_log_file(data_dir() / "debug.log");
        log_info(std::string(HX_APP_NAME) + " " + HX_VERSION_STRING + " (debug)");
    }
    // One instance: a second launch just brings the first one forward.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Hexwright.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND w = FindWindowW(L"HexwrightWindow", nullptr)) {
            ShowWindow(w, SW_RESTORE);
            SetForegroundWindow(w);
        }
        return 0;
    }
    if (!ui::gfx::init()) {
        MessageBoxW(nullptr, L"Hexwright couldn't start its graphics. A Direct3D 11 capable GPU or driver is required.", L"Hexwright", MB_ICONERROR);
        return 1;
    }
    AppState state;
    state.start();

    ui::WindowDesc d;
    d.title = L"Hexwright";
    d.width = 1180;
    d.height = 780;
    d.min_width = 960;
    d.min_height = 620;
    d.visible = !start_minimized;
    auto main_window = ui::UiWindow::create(d);
    if (!main_window) return 2;
    ui::apply_theme(ui::gfx::dpi_scale());
    if (start_minimized) ShowWindow(main_window->hwnd(), SW_SHOWMINNOACTIVE);

    ui::App app(state);
    std::unique_ptr<ui::UiWindow> overlay;
    bool overlay_ready = false;

    while (ui::pump_messages() && !state.quit) {
        if (!main_window->frame([&] { app.draw(main_window->fonts()); })) break;

        // Overlay: a small always-on-top list of the active cheats while a game is hooked.
        auto tr = state.trainer();
        bool want = state.overlay_wanted && tr && tr->hooked();
        if (want && !overlay) {
            ui::WindowDesc od;
            od.title = L"Hexwright overlay";
            od.overlay = true;
            od.width = 300;
            od.height = 240;
            od.x = 24;
            od.y = 24;
            overlay = ui::UiWindow::create(od);
            overlay_ready = overlay != nullptr;
            if (overlay) {
                ImGui::SetCurrentContext(overlay->context());
                ui::apply_theme(ui::gfx::dpi_scale());
            }
        }
        if (overlay) {
            if (want != overlay->visible()) overlay->show(want);
            if (want && overlay_ready) overlay->frame([&] { app.draw_overlay(overlay->fonts()); });
        }
    }
    overlay.reset();
    state.stop();
    main_window.reset();
    ui::gfx::shutdown();
    if (mutex) CloseHandle(mutex);
    return 0;
}

}  // namespace

static int parse_and_run() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool debug =
#ifdef HX_DEBUG_BUILD
        true;
#else
        false;
#endif
    bool minimized = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--debug") debug = true;
        else if (a == L"--minimized") minimized = true;
    }
    LocalFree(argv);
    return run(debug, minimized);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return parse_and_run(); }
int wmain() { return parse_and_run(); }
