#include <windows.h>

#include "app/gfx.h"
#include "app/theme.h"
#include "app/widgets.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    if (!hx::ui::gfx::init()) return 1;
    hx::ui::WindowDesc d;
    d.title = L"Hexwright";
    d.min_width = 900;
    d.min_height = 600;
    auto w = hx::ui::UiWindow::create(d);
    if (!w) return 2;
    hx::ui::apply_theme(hx::ui::gfx::dpi_scale());
    while (hx::ui::pump_messages()) {
        if (!w->frame([&] {
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
                ImGui::Begin("root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
                ImGui::SetCursorPos(ImVec2(30, 30));
                ImGui::TextUnformatted("Hexwright");
                ImGui::End();
            }))
            break;
    }
    hx::ui::gfx::shutdown();
    return 0;
}
