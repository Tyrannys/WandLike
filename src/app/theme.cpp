#include "app/theme.h"

namespace hx::ui {

void apply_theme(float s) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(0, 0);
    st.FramePadding = ImVec2(10, 7);
    st.ItemSpacing = ImVec2(10, 8);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.WindowRounding = 0;
    st.ChildRounding = 10;
    st.FrameRounding = 8;
    st.PopupRounding = 10;
    st.GrabRounding = 8;
    st.ScrollbarRounding = 8;
    st.ScrollbarSize = 10;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 0;
    st.PopupBorderSize = 1;
    st.FrameBorderSize = 0;
    st.ScaleAllSizes(s);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = col::text;
    c[ImGuiCol_TextDisabled] = col::dim;
    c[ImGuiCol_WindowBg] = col::bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = col::panel;
    c[ImGuiCol_Border] = col::line;
    c[ImGuiCol_FrameBg] = col::field;
    c[ImGuiCol_FrameBgHovered] = col::button;
    c[ImGuiCol_FrameBgActive] = col::button_hi;
    c[ImGuiCol_TitleBg] = col::side;
    c[ImGuiCol_TitleBgActive] = col::side;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = rgb(0x232733);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(0x2d3341);
    c[ImGuiCol_ScrollbarGrabActive] = rgb(0x363d4d);
    c[ImGuiCol_CheckMark] = col::accent;
    c[ImGuiCol_SliderGrab] = col::accent;
    c[ImGuiCol_SliderGrabActive] = col::accent_hi;
    c[ImGuiCol_Button] = col::button;
    c[ImGuiCol_ButtonHovered] = col::button_hi;
    c[ImGuiCol_ButtonActive] = rgb(0x30364a);
    c[ImGuiCol_Header] = col::row;
    c[ImGuiCol_HeaderHovered] = col::row_hi;
    c[ImGuiCol_HeaderActive] = col::row_hi;
    c[ImGuiCol_Separator] = col::line;
    c[ImGuiCol_Tab] = col::row;
    c[ImGuiCol_TabHovered] = col::row_hi;
    c[ImGuiCol_TextSelectedBg] = rgb(0x27c4da, 0.35f);
    c[ImGuiCol_ModalWindowDimBg] = rgb(0x000000, 0.65f);
    c[ImGuiCol_NavHighlight] = ImVec4(0, 0, 0, 0);
}

}  // namespace hx::ui
