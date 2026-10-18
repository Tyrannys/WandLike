#include "app/widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/hotkeys.h"
#include "core/util.h"

namespace hx::ui {

// ------------------------------------------------------------------- icons
void draw_icon(ImDrawList* dl, Icon icon, ImVec2 c, float s, ImU32 col) {
    auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    const float th = std::max(1.5f, s * 0.085f);
    switch (icon) {
        case Icon::Bolt: {
            ImVec2 pts[] = {P(0.12f, -0.5f), P(-0.32f, 0.06f), P(-0.02f, 0.06f), P(-0.12f, 0.5f), P(0.32f, -0.08f), P(0.02f, -0.08f)};
            dl->AddConvexPolyFilled(pts, 6, col);
            break;
        }
        case Icon::Star: case Icon::StarFilled: {
            ImVec2 pts[10];
            for (int i = 0; i < 10; ++i) {
                float a = -1.5708f + i * 0.62832f, r = i % 2 == 0 ? 0.5f : 0.22f;
                pts[i] = P(std::cos(a) * r, std::sin(a) * r + 0.04f);
            }
            if (icon == Icon::StarFilled) dl->AddConvexPolyFilled(pts, 10, col);
            else dl->AddPolyline(pts, 10, col, ImDrawFlags_Closed, th * 0.8f);
            break;
        }
        case Icon::Pin: case Icon::PinFilled: {
            ImVec2 head[] = {P(-0.2f, -0.5f), P(0.2f, -0.5f), P(0.14f, -0.05f), P(0.32f, 0.12f), P(-0.32f, 0.12f), P(-0.14f, -0.05f)};
            if (icon == Icon::PinFilled) dl->AddConvexPolyFilled(head, 6, col);
            else dl->AddPolyline(head, 6, col, ImDrawFlags_Closed, th * 0.8f);
            dl->AddLine(P(0, 0.12f), P(0, 0.5f), col, th);
            break;
        }
        case Icon::Play: {
            ImVec2 pts[] = {P(-0.28f, -0.42f), P(0.4f, 0), P(-0.28f, 0.42f)};
            dl->AddConvexPolyFilled(pts, 3, col);
            break;
        }
        case Icon::Link: case Icon::Unlink: {
            float gap = icon == Icon::Unlink ? 0.12f : 0.0f;
            dl->AddCircle(P(-0.17f - gap, 0), s * 0.2f, col, 0, th);
            dl->AddCircle(P(0.17f + gap, 0), s * 0.2f, col, 0, th);
            if (icon == Icon::Link) dl->AddLine(P(-0.17f, 0), P(0.17f, 0), col, th);
            else dl->AddLine(P(-0.42f, 0.42f), P(0.42f, -0.42f), col, th);
            break;
        }
        case Icon::Search:
            dl->AddCircle(P(-0.08f, -0.08f), s * 0.28f, col, 0, th);
            dl->AddLine(P(0.14f, 0.14f), P(0.44f, 0.44f), col, th * 1.2f);
            break;
        case Icon::Check: {
            ImVec2 pts[] = {P(-0.38f, 0.02f), P(-0.1f, 0.3f), P(0.4f, -0.26f)};
            dl->AddPolyline(pts, 3, col, 0, th * 1.3f);
            break;
        }
        case Icon::Minus: dl->AddLine(P(-0.3f, 0), P(0.3f, 0), col, th * 1.2f); break;
        case Icon::Plus:
            dl->AddLine(P(-0.3f, 0), P(0.3f, 0), col, th * 1.2f);
            dl->AddLine(P(0, -0.3f), P(0, 0.3f), col, th * 1.2f);
            break;
        case Icon::Gear:
            dl->AddCircle(c, s * 0.2f, col, 0, th);
            for (int i = 0; i < 8; ++i) {
                float a = i * 0.7854f;
                dl->AddLine(P(std::cos(a) * 0.33f, std::sin(a) * 0.33f), P(std::cos(a) * 0.47f, std::sin(a) * 0.47f), col, th * 1.3f);
            }
            dl->AddCircle(c, s * 0.34f, col, 0, th * 0.9f);
            break;
        case Icon::Keyboard:
            dl->AddRect(P(-0.5f, -0.28f), P(0.5f, 0.28f), col, s * 0.08f, 0, th * 0.9f);
            for (int i = 0; i < 4; ++i) dl->AddCircleFilled(P(-0.3f + i * 0.2f, -0.08f), s * 0.035f, col);
            dl->AddLine(P(-0.25f, 0.12f), P(0.25f, 0.12f), col, th);
            break;
        case Icon::Bookmark: {
            ImVec2 pts[] = {P(-0.28f, -0.5f), P(0.28f, -0.5f), P(0.28f, 0.5f), P(0, 0.26f), P(-0.28f, 0.5f)};
            dl->AddPolyline(pts, 5, col, ImDrawFlags_Closed, th);
            break;
        }
        case Icon::Layers: {
            ImVec2 a[] = {P(0, -0.46f), P(0.5f, -0.18f), P(0, 0.1f), P(-0.5f, -0.18f)};
            dl->AddPolyline(a, 4, col, ImDrawFlags_Closed, th);
            dl->AddLine(P(-0.5f, 0.08f), P(0, 0.36f), col, th);
            dl->AddLine(P(0, 0.36f), P(0.5f, 0.08f), col, th);
            break;
        }
        case Icon::Info:
            dl->AddCircle(c, s * 0.44f, col, 0, th);
            dl->AddLine(P(0, -0.04f), P(0, 0.22f), col, th * 1.2f);
            dl->AddCircleFilled(P(0, -0.2f), s * 0.05f, col);
            break;
        case Icon::ChevronDown: {
            ImVec2 pts[] = {P(-0.3f, -0.12f), P(0, 0.18f), P(0.3f, -0.12f)};
            dl->AddPolyline(pts, 3, col, 0, th * 1.2f);
            break;
        }
        case Icon::ChevronRight: {
            ImVec2 pts[] = {P(-0.12f, -0.3f), P(0.18f, 0), P(-0.12f, 0.3f)};
            dl->AddPolyline(pts, 3, col, 0, th * 1.2f);
            break;
        }
        case Icon::Home: {
            ImVec2 roof[] = {P(-0.5f, -0.02f), P(0, -0.46f), P(0.5f, -0.02f)};
            dl->AddPolyline(roof, 3, col, 0, th);
            ImVec2 body[] = {P(-0.34f, -0.1f), P(-0.34f, 0.44f), P(0.34f, 0.44f), P(0.34f, -0.1f)};
            dl->AddPolyline(body, 4, col, 0, th);
            break;
        }
        case Icon::Grid:
            for (int i = 0; i < 4; ++i) {
                float x = (i % 2) * 0.5f - 0.46f, y = (i / 2) * 0.5f - 0.46f;
                dl->AddRect(P(x, y), P(x + 0.42f, y + 0.42f), col, s * 0.06f, 0, th * 0.9f);
            }
            break;
        case Icon::Compass: {
            dl->AddCircle(c, s * 0.46f, col, 0, th);
            ImVec2 pts[] = {P(0.18f, -0.18f), P(0.06f, 0.06f), P(-0.18f, 0.18f), P(-0.06f, -0.06f)};
            dl->AddConvexPolyFilled(pts, 4, col);
            break;
        }
        case Icon::Target:
            dl->AddCircle(c, s * 0.3f, col, 0, th);
            dl->AddLine(P(-0.5f, 0), P(-0.18f, 0), col, th);
            dl->AddLine(P(0.18f, 0), P(0.5f, 0), col, th);
            dl->AddLine(P(0, -0.5f), P(0, -0.18f), col, th);
            dl->AddLine(P(0, 0.18f), P(0, 0.5f), col, th);
            dl->AddCircleFilled(c, s * 0.05f, col);
            break;
        case Icon::Close:
            dl->AddLine(P(-0.28f, -0.28f), P(0.28f, 0.28f), col, th * 1.2f);
            dl->AddLine(P(-0.28f, 0.28f), P(0.28f, -0.28f), col, th * 1.2f);
            break;
        case Icon::Trash:
            dl->AddRect(P(-0.28f, -0.2f), P(0.28f, 0.5f), col, s * 0.05f, 0, th);
            dl->AddLine(P(-0.42f, -0.28f), P(0.42f, -0.28f), col, th);
            dl->AddLine(P(-0.12f, -0.28f), P(-0.12f, -0.44f), col, th);
            dl->AddLine(P(0.12f, -0.28f), P(0.12f, -0.44f), col, th);
            dl->AddLine(P(-0.12f, -0.4f), P(0.12f, -0.4f), col, th);
            break;
        case Icon::Refresh:
            dl->PathArcTo(c, s * 0.34f, -0.5f, 4.4f, 24);
            dl->PathStroke(col, 0, th);
            {
                ImVec2 tip = P(0.3f, -0.24f);
                ImVec2 pts[] = {ImVec2(tip.x - s * 0.02f, tip.y - s * 0.22f), tip, ImVec2(tip.x - s * 0.24f, tip.y - s * 0.04f)};
                dl->AddPolyline(pts, 3, col, 0, th);
            }
            break;
        case Icon::Save: {
            dl->AddRect(P(-0.42f, -0.42f), P(0.42f, 0.42f), col, s * 0.06f, 0, th);
            dl->AddRect(P(-0.22f, -0.42f), P(0.2f, -0.12f), col, 0, 0, th);
            dl->AddRect(P(-0.24f, 0.08f), P(0.24f, 0.42f), col, 0, 0, th);
            break;
        }
        case Icon::Folder: {
            ImVec2 pts[] = {P(-0.5f, -0.3f), P(-0.16f, -0.3f), P(-0.04f, -0.18f), P(0.5f, -0.18f), P(0.5f, 0.4f), P(-0.5f, 0.4f)};
            dl->AddPolyline(pts, 6, col, ImDrawFlags_Closed, th);
            break;
        }
        case Icon::Sliders:
            for (int i = 0; i < 3; ++i) {
                float y = -0.3f + i * 0.3f, kx = (i == 1 ? 0.12f : i == 0 ? -0.18f : 0.22f);
                dl->AddLine(P(-0.45f, y), P(0.45f, y), col, th * 0.9f);
                dl->AddCircleFilled(P(kx, y), s * 0.09f, col);
            }
            break;
        case Icon::Shield: {
            ImVec2 pts[] = {P(0, -0.5f), P(0.4f, -0.34f), P(0.36f, 0.14f), P(0, 0.5f), P(-0.36f, 0.14f), P(-0.4f, -0.34f)};
            dl->AddPolyline(pts, 6, col, ImDrawFlags_Closed, th);
            break;
        }
        case Icon::Download:
            dl->AddLine(P(0, -0.46f), P(0, 0.2f), col, th * 1.1f);
            {
                ImVec2 pts[] = {P(-0.24f, -0.04f), P(0, 0.22f), P(0.24f, -0.04f)};
                dl->AddPolyline(pts, 3, col, 0, th * 1.1f);
            }
            dl->AddLine(P(-0.4f, 0.44f), P(0.4f, 0.44f), col, th * 1.1f);
            break;
    }
}

bool icon_button(const char* id, Icon icon, const char* tooltip, bool active, float size) {
    float s = size * gfx::dpi_scale();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(s, s));
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), u32(hov ? col::button_hi : col::button), 9.0f * gfx::dpi_scale());
    ImVec4 c = active ? col::warn : hov ? col::text : col::dim;
    draw_icon(dl, icon, ImVec2(p.x + s / 2, p.y + s / 2), s * 0.46f, u32(c));
    if (hov && tooltip && *tooltip) tooltip_text(tooltip);
    return clicked;
}

void tooltip_text(const char* text) {
    if (!text || !*text) return;
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(360 * gfx::dpi_scale(), 600));
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(340 * gfx::dpi_scale());
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// ----------------------------------------------------------------- buttons
static bool colored_button(const char* label, ImVec2 size, const ImVec4& bg, const ImVec4& hover, const ImVec4& active, const ImVec4& text,
                           bool enabled, bool bold) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems) return false;
    ImFont* font = ImGui::GetIO().FontDefault;
    (void)font;
    const char* end = ImGui::FindRenderedTextEnd(label);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    float pad = 16.0f * gfx::dpi_scale();
    ImVec2 sz(size.x > 0 ? size.x : ts.x + pad * 2, size.y > 0 ? size.y : ImGui::GetFrameHeight() + 4 * gfx::dpi_scale());
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##b", sz);
    ImGui::PopID();
    bool hov = enabled && ImGui::IsItemHovered(), act = enabled && ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 fill = !enabled ? ImVec4(bg.x, bg.y, bg.z, bg.w * 0.45f) : act ? active : hov ? hover : bg;
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), u32(fill), 9.0f * gfx::dpi_scale());
    ImVec4 tc = enabled ? text : ImVec4(text.x, text.y, text.z, 0.45f);
    (void)bold;
    dl->AddText(ImVec2(p.x + (sz.x - ts.x) / 2, p.y + (sz.y - ts.y) / 2), u32(tc), label, end);
    return clicked && enabled;
}

bool primary_button(const char* l, ImVec2 s, bool e) { return colored_button(l, s, col::accent, col::accent_hi, rgb(0x6fe0ef), rgb(0x04252c), e, true); }
bool secondary_button(const char* l, ImVec2 s, bool e) { return colored_button(l, s, col::button, col::button_hi, rgb(0x30364a), col::text, e, false); }
bool accent_soft_button(const char* l, ImVec2 s) { return colored_button(l, s, col::accent_dk, rgb(0x155a6c), rgb(0x1b6e84), col::accent, true, true); }
bool success_button(const char* l, ImVec2 s) { return colored_button(l, s, col::ok_bg, rgb(0x234b3d), rgb(0x2a5a49), col::ok, true, true); }

// ---------------------------------------------------------------- switches
bool toggle_switch(const char* id, bool* v) {
    float h = ImGui::GetFrameHeight() * 0.8f, w = h * 1.9f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    p.y += (ImGui::GetFrameHeight() - h) * 0.5f;
    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("##sw", ImVec2(w, h));
    ImGui::PopID();
    if (clicked) *v = !*v;
    ImGuiStorage* st = ImGui::GetStateStorage();
    ImGuiID key = ImGui::GetID(id);
    float t = st->GetFloat(key, *v ? 1.0f : 0.0f);
    t += ((*v ? 1.0f : 0.0f) - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
    st->SetFloat(key, t);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 bg(col::button_hi.x + (col::accent_dk.x - col::button_hi.x) * t, col::button_hi.y + (col::accent_dk.y - col::button_hi.y) * t,
              col::button_hi.z + (col::accent_dk.z - col::button_hi.z) * t, 1.0f);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), u32(bg), h);
    ImVec4 knob(0.54f + (col::accent.x - 0.54f) * t, 0.57f + (col::accent.y - 0.57f) * t, 0.64f + (col::accent.z - 0.64f) * t, 1.0f);
    float r = h * 0.38f;
    dl->AddCircleFilled(ImVec2(p.x + h * 0.5f + (w - h) * t, p.y + h * 0.5f), r, u32(knob));
    return clicked;
}

bool seg_off_on(const char* id, bool* on, bool enabled) {
    float sc = gfx::dpi_scale();
    float segw = 48 * sc, h = 30 * sc;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + segw * 2 + 4 * sc, p.y + h), u32(col::field), 8 * sc);
    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < 2; ++i) {
        bool is_on_seg = i == 1;
        ImVec2 a(p.x + 2 * sc + i * segw, p.y + 2 * sc), b(a.x + segw, p.y + h - 2 * sc);
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton(is_on_seg ? "on" : "off", ImVec2(segw, h - 4 * sc)) && enabled && *on != is_on_seg) { *on = is_on_seg; changed = true; }
        bool active = *on == is_on_seg;
        bool hov = ImGui::IsItemHovered() && enabled;
        if (active) dl->AddRectFilled(a, b, u32(is_on_seg ? col::accent_dk : rgb(0x262b36)), 6 * sc);
        else if (hov) dl->AddRectFilled(a, b, u32(rgb(0x1a1e27)), 6 * sc);
        const char* label = is_on_seg ? "On" : "Off";
        ImVec2 ts = ImGui::CalcTextSize(label);
        ImVec4 tc = active ? (is_on_seg ? col::accent : col::text) : col::dimmer;
        dl->AddText(ImVec2(a.x + (segw - ts.x) / 2, a.y + (b.y - a.y - ts.y) / 2), u32(tc), label);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y));
    ImGui::Dummy(ImVec2(segw * 2 + 4 * sc, h));
    return changed;
}

// ------------------------------------------------------------------ numbers
std::string format_value_for_display(double v, bool is_int) {
    char b[48];
    if (is_int) std::snprintf(b, sizeof b, "%lld", (long long)std::llround(v));
    else std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

void NumberField::set(double v, bool is_int) {
    std::string s = format_value_for_display(v, is_int);
    std::snprintf(buf, sizeof buf, "%s", s.c_str());
    inited = true;
}

std::optional<double> NumberField::parse() const {
    char* end = nullptr;
    std::string s = trim(buf);
    if (s.empty()) return std::nullopt;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end) return std::nullopt;
    return v;
}

StepperResult stepper(const char* id, NumberField& f, double step, double lo, double hi, bool is_int, bool enabled) {
    StepperResult r;
    float sc = gfx::dpi_scale();
    float h = 30 * sc, bw = 30 * sc, iw = 96 * sc;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + bw * 2 + iw, p.y + h), u32(col::field), 8 * sc);
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    auto step_button = [&](const char* bid, float x, Icon ic, double delta) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + x, p.y));
        if (ImGui::InvisibleButton(bid, ImVec2(bw, h))) {
            double cur = f.parse().value_or(0);
            f.set(std::clamp(cur + delta, lo, hi), is_int);
            r.changed = true;
        }
        bool hov = ImGui::IsItemHovered();
        draw_icon(dl, ic, ImVec2(p.x + x + bw / 2, p.y + h / 2), 15 * sc, u32(hov ? col::text : col::dim));
    };
    step_button("minus", 0, Icon::Minus, -step);
    step_button("plus", bw + iw, Icon::Plus, step);
    // value text, centred while not being edited
    ImGui::SetCursorScreenPos(ImVec2(p.x + bw, p.y));
    ImGui::SetNextItemWidth(iw);
    ImVec2 ts = ImGui::CalcTextSize(f.buf);
    float padx = std::max(4.0f * sc, (iw - ts.x) * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padx, (h - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue;
    bool enter = ImGui::InputText("##v", f.buf, sizeof f.buf, flags);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    if (enter) r.apply = true;
    ImGui::SameLine(0, 8 * sc);
    ImGui::SetCursorScreenPos(ImVec2(p.x + bw * 2 + iw + 8 * sc, p.y));
    {
        ImVec2 ap = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("apply", ImVec2(h, h))) r.apply = true;
        bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(ap, ImVec2(ap.x + h, ap.y + h), u32(hov ? rgb(0x155a6c) : col::accent_dk), 8 * sc);
        draw_icon(dl, Icon::Check, ImVec2(ap.x + h / 2, ap.y + h / 2), 16 * sc, u32(col::accent));
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return r;
}

// ------------------------------------------------------------------- misc
void chip(const char* text, const ImVec4& color, const ImVec4& bg) {
    float sc = gfx::dpi_scale();
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float padx = 10 * sc, h = ts.y + 6 * sc;
    ImGui::Dummy(ImVec2(ts.x + padx * 2, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + ts.x + padx * 2, p.y + h), u32(bg), h);
    dl->AddText(ImVec2(p.x + padx, p.y + 3 * sc), u32(color), text);
}

bool search_box(const char* id, char* buf, size_t n, const char* hint, float width) {
    float sc = gfx::dpi_scale();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = 38 * sc;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), u32(col::row), 9 * sc);
    draw_icon(dl, Icon::Search, ImVec2(p.x + 20 * sc, p.y + h / 2), 15 * sc, u32(col::dim));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 38 * sc, p.y));
    ImGui::SetNextItemWidth(width - 48 * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, (h - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    bool changed = ImGui::InputTextWithHint(id, hint, buf, n);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(width, 0));
    return changed;
}

void section_header(const char* label, Icon icon, bool* collapsed) {
    float sc = gfx::dpi_scale();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = 40 * sc;
    ImGui::PushID(label);
    if (ImGui::InvisibleButton("##sec", ImVec2(w, h))) *collapsed = !*collapsed;
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw_icon(dl, icon, ImVec2(p.x + 22 * sc, p.y + h / 2 + 3 * sc), 15 * sc, u32(col::dim));
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : ImGui::GetFont());
    dl->AddText(ImVec2(p.x + 42 * sc, p.y + h / 2 - ImGui::GetTextLineHeight() / 2 + 3 * sc), u32(col::text), label);
    ImGui::PopFont();
    draw_icon(dl, *collapsed ? Icon::ChevronRight : Icon::ChevronDown, ImVec2(p.x + w - 20 * sc, p.y + h / 2 + 3 * sc), 16 * sc, u32(col::dim));
}

void banner(const char* text, const ImVec4& color, const ImVec4& bg) {
    float sc = gfx::dpi_scale();
    float w = ImGui::GetContentRegionAvail().x;
    float pad = 14 * sc;
    ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, w - pad * 2);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ts.y + pad * 1.4f;
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), u32(bg), 9 * sc);
    ImGui::SetCursorScreenPos(ImVec2(p.x + pad, p.y + pad * 0.7f));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(p.x + w - pad);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(w, 0));
}

bool cover_card(const char* id, const Texture* cover, const char* title, const char* subtitle, const char* badge, bool badge_live, bool favorite,
                ImVec2 size) {
    float sc = gfx::dpi_scale();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("##card", size);
    ImGui::PopID();
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float round = 12 * sc;
    ImVec2 q(p.x + size.x, p.y + size.y);
    if (hov) p.y -= 3 * sc, q.y -= 3 * sc;
    if (cover) {
        dl->AddImageRounded((ImTextureID)cover->srv, p, q, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, round);
    } else {
        dl->AddRectFilledMultiColor(p, q, u32(rgb(0x1d2430)), u32(rgb(0x1d2430)), u32(rgb(0x11141a)), u32(rgb(0x11141a)));
        std::string ini;
        for (auto& w : split(title, ' ')) if (!w.empty() && ini.size() < 3) ini.push_back((char)std::toupper((unsigned char)w[0]));
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 3 ? ImGui::GetIO().Fonts->Fonts[3] : ImGui::GetFont());
        ImVec2 ts = ImGui::CalcTextSize(ini.c_str());
        dl->AddText(ImVec2(p.x + (size.x - ts.x) / 2, p.y + (size.y - ts.y) / 2 - 20 * sc), u32(rgb(0x2b3342)), ini.c_str());
        ImGui::PopFont();
    }
    dl->AddRectFilledMultiColor(ImVec2(p.x, p.y + size.y * 0.5f), q, 0, 0, IM_COL32(0, 0, 0, 215), IM_COL32(0, 0, 0, 215));
    dl->AddRect(p, q, u32(hov ? rgb(0x3a4252) : col::line), round, 0, 1.0f);
    dl->AddText(ImVec2(p.x + 12 * sc, q.y - 44 * sc), IM_COL32_WHITE, title);
    dl->AddText(ImVec2(p.x + 12 * sc, q.y - 24 * sc), IM_COL32(185, 192, 207, 255), subtitle);
    if (badge && *badge) {
        ImVec2 ts = ImGui::CalcTextSize(badge);
        ImVec2 a(p.x + 10 * sc, p.y + 10 * sc), b(a.x + ts.x + 18 * sc, a.y + ts.y + 6 * sc);
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 170), 20 * sc);
        dl->AddText(ImVec2(a.x + 9 * sc, a.y + 3 * sc), u32(badge_live ? col::ok : col::dim), badge);
    }
    if (favorite) draw_icon(dl, Icon::StarFilled, ImVec2(q.x - 20 * sc, p.y + 20 * sc), 16 * sc, u32(col::warn));
    return clicked;
}

// ------------------------------------------------------------- hotkey capture
namespace {
struct KeyName { ImGuiKey key; const char* name; };

const char* key_name(ImGuiKey k, char* scratch, size_t n) {
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) { std::snprintf(scratch, n, "%c", 'A' + (k - ImGuiKey_A)); return scratch; }
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) { std::snprintf(scratch, n, "%c", '0' + (k - ImGuiKey_0)); return scratch; }
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F24) { std::snprintf(scratch, n, "F%d", 1 + (k - ImGuiKey_F1)); return scratch; }
    if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) { std::snprintf(scratch, n, "Num%d", k - ImGuiKey_Keypad0); return scratch; }
    static const KeyName table[] = {
        {ImGuiKey_Space, "Space"}, {ImGuiKey_Enter, "Enter"}, {ImGuiKey_Tab, "Tab"}, {ImGuiKey_Backspace, "Backspace"}, {ImGuiKey_Insert, "Insert"},
        {ImGuiKey_Delete, "Delete"}, {ImGuiKey_Home, "Home"}, {ImGuiKey_End, "End"}, {ImGuiKey_PageUp, "PageUp"}, {ImGuiKey_PageDown, "PageDown"},
        {ImGuiKey_LeftArrow, "Left"}, {ImGuiKey_RightArrow, "Right"}, {ImGuiKey_UpArrow, "Up"}, {ImGuiKey_DownArrow, "Down"},
        {ImGuiKey_GraveAccent, "`"}, {ImGuiKey_Minus, "-"}, {ImGuiKey_Equal, "="}, {ImGuiKey_LeftBracket, "["}, {ImGuiKey_RightBracket, "]"},
        {ImGuiKey_Semicolon, ";"}, {ImGuiKey_Apostrophe, "'"}, {ImGuiKey_Comma, ","}, {ImGuiKey_Period, "."}, {ImGuiKey_Slash, "/"},
        {ImGuiKey_Backslash, "\\"}};
    for (auto& e : table) if (e.key == k) return e.name;
    return nullptr;
}
}  // namespace

std::optional<std::string> capture_hotkey_popup(const char* title, const std::string& current, bool* open) {
    if (!*open) return std::nullopt;
    float sc = gfx::dpi_scale();
    if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420 * sc, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * sc, 20 * sc));
    std::optional<std::string> result;
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : ImGui::GetFont());
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, col::dim);
        ImGui::TextUnformatted("Press the key combination you want. Esc cancels.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 8 * sc));
        ImGuiIO& io = ImGui::GetIO();
        std::string live;
        if (io.KeyCtrl) live += "Ctrl+";
        if (io.KeyAlt) live += "Alt+";
        if (io.KeyShift) live += "Shift+";
        if (io.KeySuper) live += "Win+";
        ImVec2 box = ImGui::GetCursorScreenPos();
        float bw = ImGui::GetContentRegionAvail().x, bh = 64 * sc;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(box, ImVec2(box.x + bw, box.y + bh), u32(rgb(0x0d0f14)), 10 * sc);
        dl->AddRect(box, ImVec2(box.x + bw, box.y + bh), u32(rgb(0x343b4a)), 10 * sc);
        std::string shown = live.empty() ? (current.empty() ? "..." : current) : live + "...";
        ImVec2 ts = ImGui::CalcTextSize(shown.c_str());
        dl->AddText(ImVec2(box.x + (bw - ts.x) / 2, box.y + (bh - ts.y) / 2), IM_COL32_WHITE, shown.c_str());
        ImGui::Dummy(ImVec2(bw, bh + 12 * sc));

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { *open = false; ImGui::CloseCurrentPopup(); }
        else {
            char scratch[16];
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                ImGuiKey key = (ImGuiKey)k;
                if (!ImGui::IsKeyPressed(key, false)) continue;
                const char* name = key_name(key, scratch, sizeof scratch);
                if (!name) continue;
                result = live + name;
                *open = false;
                ImGui::CloseCurrentPopup();
                break;
            }
        }
        if (*open) {
            if (secondary_button("Clear", ImVec2(90 * sc, 0))) { result = std::string(); *open = false; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (secondary_button("Cancel", ImVec2(90 * sc, 0))) { *open = false; ImGui::CloseCurrentPopup(); }
        }
        ImGui::EndPopup();
    } else {
        *open = false;
    }
    ImGui::PopStyleVar();
    return result;
}

}  // namespace hx::ui
