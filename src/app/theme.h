#pragma once
#include <imgui.h>

namespace hx::ui {

constexpr ImVec4 rgb(unsigned hex, float a = 1.0f) {
    return ImVec4(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, a);
}

// Palette (matches the look of the earlier web UI).
namespace col {
inline const ImVec4 bg = rgb(0x0b0c10), side = rgb(0x0e1015), panel = rgb(0x12141a), row = rgb(0x15171e), row_hi = rgb(0x1b1e27);
inline const ImVec4 line = rgb(0x20242d), text = rgb(0xe9ebf0), dim = rgb(0x7d8494), dimmer = rgb(0x4d5361);
inline const ImVec4 accent = rgb(0x27c4da), accent_hi = rgb(0x4ad3e6), accent_dk = rgb(0x0f4452), accent_bg = rgb(0x123a45);
inline const ImVec4 ok = rgb(0x3ecf8e), ok_bg = rgb(0x1b3a2f), warn = rgb(0xe8b14a), warn_bg = rgb(0x2a2415), bad = rgb(0xef5b5b);
inline const ImVec4 field = rgb(0x101218), button = rgb(0x1d212b), button_hi = rgb(0x262b37);
}  // namespace col

inline ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

void apply_theme(float dpi_scale);

}  // namespace hx::ui
