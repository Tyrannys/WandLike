#pragma once
// Reusable ImGui widgets in the app's visual style.
#include <imgui.h>

#include <optional>
#include <string>

#include "app/gfx.h"
#include "app/theme.h"

namespace hx::ui {

enum class Icon { Bolt, Star, StarFilled, Pin, PinFilled, Play, Link, Unlink, Search, Check, Minus, Plus, Gear, Keyboard, Bookmark, Layers, Info,
                  ChevronDown, ChevronRight, Home, Grid, Compass, Target, Close, Trash, Refresh, Save, Folder, Sliders, Shield, Download };

void draw_icon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);
// A square clickable icon button. `active` tints it with the accent colour. Returns true when clicked.
bool icon_button(const char* id, Icon icon, const char* tooltip, bool active = false, float size = 34.0f);
void tooltip_text(const char* text);                                 // wrapped tooltip when the previous item is hovered

bool primary_button(const char* label, ImVec2 size = ImVec2(0, 0), bool enabled = true);
bool secondary_button(const char* label, ImVec2 size = ImVec2(0, 0), bool enabled = true);
bool accent_soft_button(const char* label, ImVec2 size = ImVec2(0, 0));         // dark teal fill, bright text
bool success_button(const char* label, ImVec2 size = ImVec2(0, 0));             // used for "Unhook"

bool toggle_switch(const char* id, bool* v);                         // iOS-style switch
// Segmented Off | On control; returns true if the user clicked a segment (new state in *on).
bool seg_off_on(const char* id, bool* on, bool enabled = true);

// Number editing: a pill with - [value] +, plus an apply button. The caller keeps the NumberField alive.
struct NumberField {
    char buf[40] = "";
    bool inited = false;
    void set(double v, bool is_int);
    [[nodiscard]] std::optional<double> parse() const;
};
struct StepperResult { bool apply = false; bool changed = false; };
StepperResult stepper(const char* id, NumberField& f, double step, double lo, double hi, bool is_int, bool enabled = true);

void chip(const char* text, const ImVec4& color, const ImVec4& background);
bool search_box(const char* id, char* buf, size_t n, const char* hint, float width);
void section_header(const char* label, Icon icon, bool* collapsed);
void banner(const char* text, const ImVec4& color, const ImVec4& background);
bool cover_card(const char* id, const Texture* cover, const char* title, const char* subtitle, const char* badge, bool badge_live, bool favorite,
                ImVec2 size);

// Keyboard shortcut capture. Call every frame while `open`; returns the combo ("Ctrl+Shift+F5"), "" to clear, or nullopt while waiting.
// Esc cancels (sets *open = false without a result).
std::optional<std::string> capture_hotkey_popup(const char* title, const std::string& current, bool* open);

std::string format_value_for_display(double v, bool is_int);

}  // namespace hx::ui
