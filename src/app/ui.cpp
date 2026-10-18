#include "app/ui.h"

#include <imgui_internal.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/aob.h"
#include "core/pointer.h"
#include "hx/version.h"

namespace hx::ui {

namespace {
constexpr ValueType kTypes[] = {ValueType::U8, ValueType::I16, ValueType::I32, ValueType::I64, ValueType::F32, ValueType::F64};
const char* kTypeLabels[] = {"Byte", "2 Bytes (int16)", "4 Bytes (int32)", "8 Bytes (int64)", "Float", "Double"};
struct OpItem { ScanOp op; const char* label; int values; };   // values: how many value boxes are needed
const OpItem kFirstOps[] = {{ScanOp::Exact, "Exact value", 1}, {ScanOp::Unknown, "Unknown initial value", 0}, {ScanOp::Bigger, "Bigger than...", 1},
                            {ScanOp::Smaller, "Smaller than...", 1}, {ScanOp::Between, "Value between...", 2}};
const OpItem kNextOps[] = {{ScanOp::Exact, "Exact value", 1}, {ScanOp::Increased, "Increased value", 0}, {ScanOp::Decreased, "Decreased value", 0},
                           {ScanOp::Changed, "Changed value", 0}, {ScanOp::Unchanged, "Unchanged value", 0}, {ScanOp::IncreasedBy, "Increased by...", 1},
                           {ScanOp::DecreasedBy, "Decreased by...", 1}, {ScanOp::Bigger, "Bigger than...", 1}, {ScanOp::Smaller, "Smaller than...", 1},
                           {ScanOp::Between, "Value between...", 2}};

float S() { return gfx::dpi_scale(); }

std::string fmt_count(uint64_t n) {
    std::string s = std::to_string(n), out;
    int c = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (c && c % 3 == 0) out.push_back(',');
        out.push_back(*it);
        ++c;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

bool contains_i(const std::string& hay, const char* needle) {
    if (!*needle) return true;
    return lower(hay).find(lower(needle)) != std::string::npos;
}

bool combo_box(const char* id, int* cur, const char* const* labels, int n, float width) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, labels[*cur])) {
        for (int i = 0; i < n; ++i)
            if (ImGui::Selectable(labels[i], i == *cur)) { *cur = i; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

void text_dim(const char* t) {
    ImGui::PushStyleColor(ImGuiCol_Text, col::dim);
    ImGui::TextUnformatted(t);
    ImGui::PopStyleColor();
}

void run_at_startup(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring v = L"\"" + exe_path().wstring() + L"\" --minimized";
        RegSetValueExW(k, L"Hexwright", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"Hexwright");
    }
    RegCloseKey(k);
}

// A card-like row background drawn behind whatever is submitted next. Call end_row() afterwards.
struct RowScope {
    ImDrawList* dl;
    ImVec2 p;
    float w, h;
    ImU32 col;
};
}  // namespace

// ===================================================================== frame
void App::draw(const Fonts& fonts) {
    fonts_ = fonts;
    s_.frame();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    sidebar();
    ImGui::SameLine(0, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30 * S(), 24 * S()));
    ImGui::BeginChild("##main", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    ImGui::PopStyleVar();
    switch (page_) {
        case Page::Home: page_home(); break;
        case Page::Games: page_games(); break;
        case Page::Explore: page_explore(); break;
        case Page::Game: page_game(); break;
        case Page::Scanner: page_scanner(); break;
        case Page::Settings: page_settings(); break;
    }
    ImGui::EndChild();
    ImGui::End();
    toasts();
}

void App::toasts() {
    auto ns = s_.notices();
    if (ns.empty()) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->Pos.y + vp->Size.y - 20 * S();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (auto it = ns.rbegin(); it != ns.rend(); ++it) {
        float maxw = 420 * S();
        ImVec2 ts = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, maxw);
        float w = ts.x + 32 * S(), h = ts.y + 22 * S();
        ImVec2 a(vp->Pos.x + vp->Size.x - 24 * S() - w, y - h), b(a.x + w, y);
        dl->AddRectFilled(a, b, u32(col::panel), 10 * S());
        dl->AddRect(a, b, u32(it->bad ? col::bad : col::line), 10 * S());
        dl->AddRectFilled(a, ImVec2(a.x + 4 * S(), b.y), u32(it->bad ? col::bad : col::accent), 10 * S(), ImDrawFlags_RoundCornersLeft);
        dl->AddText(nullptr, 0.0f, ImVec2(a.x + 18 * S(), a.y + 11 * S()), u32(col::text), it->text.c_str(), nullptr, maxw);
        y -= h + 8 * S();
    }
}

// ==================================================================== sidebar
void App::sidebar() {
    float w = 232 * S();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::side);
    ImGui::BeginChild("##side", ImVec2(w, 0), 0, 0);
    ImGui::PopStyleColor();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    // brand
    dl->AddRectFilled(ImVec2(p.x + 22 * S(), p.y + 24 * S()), ImVec2(p.x + 54 * S(), p.y + 56 * S()), u32(col::accent_dk), 9 * S());
    draw_icon(dl, Icon::Bolt, ImVec2(p.x + 38 * S(), p.y + 40 * S()), 20 * S(), u32(col::accent));
    ImGui::PushFont(fonts_.large);
    dl->AddText(ImVec2(p.x + 66 * S(), p.y + 24 * S()), u32(col::text), "Hexwright");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 84 * S()));

    struct Item { Page page; const char* label; Icon icon; };
    const Item items[] = {{Page::Home, "Home", Icon::Home}, {Page::Games, "My games", Icon::Grid}, {Page::Explore, "Explore", Icon::Compass},
                          {Page::Scanner, "Scanner", Icon::Target}, {Page::Settings, "Settings", Icon::Gear}};
    for (auto& it : items) {
        ImVec2 q = ImGui::GetCursorScreenPos();
        float h = 42 * S();
        ImGui::SetCursorScreenPos(ImVec2(q.x + 12 * S(), q.y));
        ImGui::PushID(it.label);
        bool clicked = ImGui::InvisibleButton("##n", ImVec2(w - 24 * S(), h));
        ImGui::PopID();
        bool hov = ImGui::IsItemHovered();
        bool sel = page_ == it.page || (page_ == Page::Game && it.page == Page::Games);
        ImVec2 a(q.x + 12 * S(), q.y), b(a.x + w - 24 * S(), q.y + h);
        if (sel) dl->AddRectFilled(a, b, u32(col::accent_bg), 10 * S());
        else if (hov) dl->AddRectFilled(a, b, u32(col::row), 10 * S());
        draw_icon(dl, it.icon, ImVec2(a.x + 24 * S(), q.y + h / 2), 18 * S(), u32(sel ? col::accent : col::dim));
        dl->AddText(ImVec2(a.x + 46 * S(), q.y + h / 2 - ImGui::GetTextLineHeight() / 2), u32(sel ? col::text : col::dim), it.label);
        if (clicked) page_ = it.page;
        ImGui::SetCursorScreenPos(ImVec2(q.x, q.y + h + 4 * S()));
    }

    // current game + status at the bottom
    float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowHeight();
    const LoadedGame* g = s_.current();
    auto tr = s_.trainer();
    if (g) {
        ImVec2 a(p.x + 14 * S(), bottom - 116 * S()), b(p.x + w - 14 * S(), bottom - 58 * S());
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##curgame", ImVec2(b.x - a.x, b.y - a.y))) page_ = Page::Game;
        dl->AddRectFilled(a, b, u32(ImGui::IsItemHovered() ? col::row_hi : col::row), 10 * S());
        dl->AddText(ImVec2(a.x + 14 * S(), a.y + 10 * S()), u32(col::text), g->def.name.c_str());
        bool hooked = tr && tr->hooked();
        bool run = s_.running_pid(*g) != 0;
        const char* st = hooked ? "Hooked" : run ? "Running, not hooked" : "Not running";
        dl->AddCircleFilled(ImVec2(a.x + 18 * S(), a.y + 40 * S()), 4 * S(), u32(hooked ? col::ok : run ? col::warn : col::dimmer));
        dl->AddText(ImVec2(a.x + 30 * S(), a.y + 32 * S()), u32(col::dim), st);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + 22 * S(), bottom - 44 * S()));
    ImGui::PushStyleColor(ImGuiCol_Text, col::dimmer);
    ImGui::Text("v%s", HX_VERSION_STRING);
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void App::page_title(const char* title, const char* subtitle) {
    ImGui::PushFont(fonts_.large);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (subtitle) text_dim(subtitle);
    ImGui::Dummy(ImVec2(0, 10 * S()));
}

const Texture* App::cover_for(const LoadedGame& g) {
    auto it = art_.find(g.def.id);
    if (it == art_.end()) {
        fs::path p;
        if (g.def.steam_appid) {
            std::string id = std::to_string(*g.def.steam_appid);
            p = steam_art_path(id, "cover");
            if (p.empty()) p = steam_art_path(id, "header");
        }
        it = art_.emplace(g.def.id, p).first;
    }
    return it->second.empty() ? nullptr : gfx::texture(it->second);
}

bool App::game_card(const LoadedGame& g, ImVec2 size) {
    bool running = s_.running_pid(g) != 0;
    std::string sub = std::to_string(g.def.cheats.size()) + " cheats";
    return cover_card(g.def.id.c_str(), cover_for(g), g.def.name.c_str(), sub.c_str(), running ? "Running" : "", true, s_.settings.favorite(g.def.id), size);
}

// ======================================================================= home
void App::page_home() {
    page_title("Welcome back", "Single-player trainers that stay out of your way.");
    {
        std::lock_guard l(s_.mu);
        if (s_.update) {
            std::string t = "Hexwright " + s_.update->version + " is available.";
            banner(t.c_str(), col::accent, col::accent_bg);
            if (accent_soft_button("Update now")) { s_.apply_update_now(); }
            ImGui::Dummy(ImVec2(0, 8 * S()));
        }
    }
    const LoadedGame* g = s_.current();
    if (g) {
        auto tr = s_.trainer();
        bool hooked = tr && tr->hooked();
        float h = 150 * S();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), u32(col::panel), 14 * S());
        if (const Texture* t = cover_for(*g)) {
            float cw = h * 0.72f;
            dl->AddImageRounded((ImTextureID)t->srv, ImVec2(p.x + 14 * S(), p.y + 14 * S()), ImVec2(p.x + 14 * S() + cw, p.y + h - 14 * S()), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 10 * S());
        }
        float tx = p.x + h * 0.72f + 36 * S();
        ImGui::PushFont(fonts_.large);
        dl->AddText(ImVec2(tx, p.y + 22 * S()), u32(col::text), g->def.name.c_str());
        ImGui::PopFont();
        std::string line = hooked ? "Hooked and ready" : s_.running_pid(*g) ? "Running, not hooked yet" : "Not running";
        dl->AddText(ImVec2(tx, p.y + 56 * S()), u32(hooked ? col::ok : col::dim), line.c_str());
        ImGui::SetCursorScreenPos(ImVec2(tx, p.y + h - 58 * S()));
        if (primary_button("Open trainer", ImVec2(150 * S(), 38 * S()))) page_ = Page::Game;
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
        ImGui::Dummy(ImVec2(w, 14 * S()));
    }
    ImGui::PushFont(fonts_.bold);
    ImGui::TextUnformatted("Your games");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 4 * S()));
    page_games();
}

// ====================================================================== games
void App::page_games() {
    if (page_ == Page::Games) page_title("My games", "Games found in your Steam, Epic and GOG libraries that have a trainer.");
    if (page_ == Page::Games) {
        search_box("##gsearch", search_, sizeof search_, "Search your games", std::min(420.0f * S(), ImGui::GetContentRegionAvail().x));
        ImGui::Dummy(ImVec2(0, 8 * S()));
    }
    std::vector<const LoadedGame*> list;
    for (auto& g : s_.store->games()) if (page_ != Page::Games || contains_i(g.def.name, search_)) list.push_back(&g);
    std::stable_sort(list.begin(), list.end(), [&](const LoadedGame* a, const LoadedGame* b) {
        bool fa = s_.settings.favorite(a->def.id), fb = s_.settings.favorite(b->def.id);
        if (fa != fb) return fa;
        return s_.settings.last_played(a->def.id) > s_.settings.last_played(b->def.id);
    });
    if (list.empty()) {
        text_dim("No games yet. Hexwright looks for installed games with a trainer definition; try Explore or rescan in Settings.");
        return;
    }
    float cw = 190 * S(), ch = 270 * S(), gap = 16 * S();
    float avail = ImGui::GetContentRegionAvail().x;
    int per = std::max(1, (int)((avail + gap) / (cw + gap)));
    int i = 0;
    for (auto* g : list) {
        if (i % per) ImGui::SameLine(0, gap);
        if (game_card(*g, ImVec2(cw, ch))) {
            s_.select_game(g->def.id);
            page_ = Page::Game;
        }
        if (ImGui::BeginPopupContextItem(("##ctx" + g->def.id).c_str())) {
            bool fav = s_.settings.favorite(g->def.id);
            if (ImGui::MenuItem(fav ? "Remove from favorites" : "Add to favorites")) s_.settings.set_favorite(g->def.id, !fav);
            ImGui::EndPopup();
        }
        ++i;
    }
    ImGui::Dummy(ImVec2(0, 8 * S()));
}

// ==================================================================== explore
void App::page_explore() {
    page_title("Explore", "Every game in the Hexwright catalog. Games you own are installed automatically.");
    {
        std::lock_guard l(s_.mu);
        text_dim(s_.catalog_status.empty() ? "Catalog not synced yet." : s_.catalog_status.c_str());
    }
    if (secondary_button("Sync catalog now", ImVec2(0, 0), !s_.syncing_catalog)) s_.sync_catalog();
    ImGui::Dummy(ImVec2(0, 10 * S()));
    std::vector<UiCatalogEntry> list;
    {
        std::lock_guard l(s_.mu);
        list = s_.explore;
    }
    if (list.empty()) text_dim("Nothing in the catalog yet.");
    for (auto& e : list) {
        float h = 58 * S(), w = ImGui::GetContentRegionAvail().x;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), u32(col::row), 10 * S());
        dl->AddText(ImVec2(p.x + 18 * S(), p.y + 10 * S()), u32(col::text), e.name.c_str());
        std::string sub = std::to_string(e.cheats) + " cheats";
        dl->AddText(ImVec2(p.x + 18 * S(), p.y + 32 * S()), u32(col::dim), sub.c_str());
        const char* tag = e.installed ? "Installed" : "Not in your library";
        ImVec2 ts = ImGui::CalcTextSize(tag);
        dl->AddText(ImVec2(p.x + w - ts.x - 18 * S(), p.y + h / 2 - ts.y / 2), u32(e.installed ? col::ok : col::dimmer), tag);
        ImGui::Dummy(ImVec2(0, 6 * S()));
    }
}

// ====================================================================== game
void App::cheat_row(const CheatDef& c, const GameDef& def, bool even) {
    auto tr = s_.trainer();
    float w = ImGui::GetContentRegionAvail().x;
    bool has_opts = c.kind == CheatKind::Toggle && !c.options.empty() && tr && tr->toggle_on(c.id);
    float h = 58 * S() + (has_opts ? 44 * S() * c.options.size() : 0);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushID(c.id.c_str());
    std::optional<std::string> gate = tr ? tr->gate(c) : std::optional<std::string>("Hook the game first.");
    if (!c.available()) gate = c.unsupported.empty() ? "Not supported yet." : c.unsupported;
    bool enabled = !gate.has_value();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), u32(even ? col::row : col::panel), 10 * S());
    ImGui::Dummy(ImVec2(w, h));
    ImVec2 end = ImGui::GetCursorScreenPos();

    float lx = p.x + 18 * S();
    dl->AddText(ImVec2(lx, p.y + 11 * S()), u32(enabled ? col::text : col::dim), c.label.c_str());
    // sub line: hotkey chip is drawn right; here the info / requirement
    std::string sub;
    if (!c.requires_.empty()) sub = "Requires " + c.requires_;
    else if (!c.info.empty()) sub = c.info;
    if (!sub.empty()) {
        ImGui::PushClipRect(ImVec2(lx, p.y), ImVec2(p.x + w * 0.5f, p.y + h), true);
        dl->AddText(ImVec2(lx, p.y + 32 * S()), u32(col::dimmer), sub.c_str());
        ImGui::PopClipRect();
    }
    // hover tooltip over the label area
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##hover", ImVec2(w * 0.5f, 58 * S()), ImGuiButtonFlags_None);
    if (ImGui::IsItemHovered()) {
        std::string tip = c.info;
        if (gate) tip += (tip.empty() ? "" : "\n\n") + *gate;
        if (c.verified != Verified::Tested && c.verified != Verified::Unsupported) tip += std::string(tip.empty() ? "" : "\n\n") + "Confidence: " + verified_name(c.verified);
        tooltip_text(tip.c_str());
    }

    // right-side controls, laid out from the right edge
    float x = p.x + w - 16 * S();
    float cy = p.y + 29 * S();
    auto place = [&](float width) { x -= width; ImGui::SetCursorScreenPos(ImVec2(x, cy - 15 * S())); x -= 12 * S(); };

    if (c.kind != CheatKind::Button) {
        // pin
        bool pinned = false;
        for (auto& pn : s_.settings.pins(def.id)) if (pn == c.id) pinned = true;
        place(34 * S());
        ImGui::SetCursorScreenPos(ImVec2(x + 12 * S(), cy - 17 * S()));
        if (icon_button("##pin", pinned ? Icon::PinFilled : Icon::Pin, pinned ? "Unpin" : "Pin to top", pinned, 34.0f)) s_.settings.set_pinned(def.id, c.id, !pinned);
    } else {
        bool pinned = false;
        for (auto& pn : s_.settings.pins(def.id)) if (pn == c.id) pinned = true;
        place(34 * S());
        ImGui::SetCursorScreenPos(ImVec2(x + 12 * S(), cy - 17 * S()));
        if (icon_button("##pin", pinned ? Icon::PinFilled : Icon::Pin, pinned ? "Unpin" : "Pin to top", pinned, 34.0f)) s_.settings.set_pinned(def.id, c.id, !pinned);
    }
    // hotkey chip
    {
        std::string hk = s_.settings.hotkey(def.id, c.id);
        const char* label = hk.empty() ? "Set key" : hk.c_str();
        ImVec2 ts = ImGui::CalcTextSize(label);
        float cw = ts.x + 24 * S();
        place(cw);
        ImGui::SetCursorScreenPos(ImVec2(x + 12 * S(), cy - 14 * S()));
        ImVec2 q = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##hk", ImVec2(cw, 28 * S()))) { capture_for_ = c.id; capture_open_ = true; }
        bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(q, ImVec2(q.x + cw, q.y + 28 * S()), u32(hov ? col::button_hi : col::button), 8 * S());
        dl->AddText(ImVec2(q.x + 12 * S(), q.y + 14 * S() - ts.y / 2), u32(hk.empty() ? col::dimmer : col::text), label);
        if (hov) tooltip_text("Click to set a global hotkey. Right-click to clear.");
        if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { s_.settings.set_hotkey(def.id, c.id, ""); s_.rebuild_hotkeys(); }
    }
    // main control
    ImGui::BeginDisabled(!enabled);
    if (c.kind == CheatKind::Toggle) {
        bool on = tr && tr->toggle_on(c.id);
        place(48 * S() * 2 + 4 * S());
        ImGui::SetCursorScreenPos(ImVec2(x + 12 * S(), cy - 15 * S()));
        if (seg_off_on("##onoff", &on, enabled)) s_.toggle_cheat(c, on);
    } else if (c.kind == CheatKind::Button) {
        place(110 * S());
        ImGui::SetCursorScreenPos(ImVec2(x + 12 * S(), cy - 17 * S()));
        if (accent_soft_button(c.launch ? "Play" : "Run", ImVec2(110 * S(), 34 * S()))) s_.press_cheat(c);
    } else {
        NumberField& f = fields_[def.id + "/" + c.id];
        if (!f.inited) f.set(s_.settings.value(def.id, c.id, c.def), true);
        std::string& mode = modes_[def.id + "/" + c.id];
        if (mode.empty()) mode = s_.settings.mode(def.id, c.id).empty() ? "set" : s_.settings.mode(def.id, c.id);
        bool int_like = c.step >= 1 || c.min == std::floor(c.min);
        place(30 * S() * 2 + 96 * S() + 8 * S() + 30 * S());
        ImGui::SetCursorScreenPos(ImVec2(x + 12 * S(), cy - 15 * S()));
        auto r = stepper("##num", f, c.step, c.min, c.max, int_like, enabled);
        if (r.apply) {
            if (auto v = f.parse()) {
                double cl = std::clamp(*v, c.min, c.max);
                f.set(cl, int_like);
                s_.apply_number(c, cl, mode);
            }
        }
        if (!c.add.empty() && !c.set.empty()) {
            place(78 * S());
            ImGui::SetCursorScreenPos(ImVec2(x + 12 * S(), cy - 15 * S()));
            bool add = mode == "add";
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * S(), 6 * S()));
            ImGui::SetNextItemWidth(78 * S());
            if (ImGui::BeginCombo("##mode", add ? "Add" : "Set")) {
                if (ImGui::Selectable("Set", !add)) mode = "set";
                if (ImGui::Selectable("Add", add)) mode = "add";
                ImGui::EndCombo();
            }
            ImGui::PopStyleVar();
        } else if (c.set.empty() && !c.add.empty()) mode = "add";
    }
    ImGui::EndDisabled();

    // toggle options (shown while the toggle is on)
    if (has_opts) {
        float oy = p.y + 58 * S();
        for (auto& o : c.options) {
            ImGui::SetCursorScreenPos(ImVec2(lx + 12 * S(), oy));
            dl->AddText(ImVec2(lx + 12 * S(), oy + 6 * S()), u32(col::dim), o.label.c_str());
            ImGui::SetCursorScreenPos(ImVec2(lx + 220 * S(), oy));
            NumberField& f = fields_[def.id + "/" + c.id + "/" + o.id];
            if (!f.inited) f.set(s_.settings.option(def.id, c.id, o.id, o.def), false);
            auto r = stepper(("##opt" + o.id).c_str(), f, std::max(1.0, (o.max - o.min) / 20.0), o.min, o.max, false, true);
            if (r.apply) if (auto v = f.parse()) {
                double cl = std::clamp(*v, o.min, o.max);
                s_.settings.set_option(def.id, c.id, o.id, cl);
                tr->set_option(c.id, o.id, cl);
            }
            oy += 44 * S();
        }
    }
    ImGui::SetCursorScreenPos(end);
    ImGui::Dummy(ImVec2(0, 6 * S()));
    ImGui::PopID();
}

void App::page_game() {
    const LoadedGame* g = s_.current();
    auto tr = s_.trainer();
    if (!g || !tr) {
        page_title("No game selected");
        text_dim("Pick a game from My games.");
        return;
    }
    if (last_game_ != g->def.id) { last_game_ = g->def.id; search_[0] = 0; fields_.clear(); modes_.clear(); }
    const GameDef& def = g->def;
    bool hooked = tr->hooked();
    uint32_t pid = s_.running_pid(*g);

    // header
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float cw = 74 * S(), chh = 100 * S();
        if (const Texture* t = cover_for(*g))
            dl->AddImageRounded((ImTextureID)t->srv, p, ImVec2(p.x + cw, p.y + chh), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 10 * S());
        else
            dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + chh), u32(col::row), 10 * S());
        ImGui::PushFont(fonts_.huge);
        dl->AddText(ImVec2(p.x + cw + 20 * S(), p.y + 2 * S()), u32(col::text), def.name.c_str());
        ImGui::PopFont();
        std::string st = hooked ? "Hooked" : pid ? "Running" : "Not running";
        dl->AddCircleFilled(ImVec2(p.x + cw + 26 * S(), p.y + 62 * S()), 5 * S(), u32(hooked ? col::ok : pid ? col::warn : col::dimmer));
        dl->AddText(ImVec2(p.x + cw + 40 * S(), p.y + 53 * S()), u32(col::dim), (st + "  ·  " + std::to_string(def.cheats.size()) + " cheats").c_str());
        // buttons (right aligned)
        float bx = p.x + w;
        auto place = [&](float bw) { bx -= bw; ImGui::SetCursorScreenPos(ImVec2(bx, p.y + 8 * S())); bx -= 10 * S(); };
        bool fav = s_.settings.favorite(def.id);
        place(34 * S());
        if (icon_button("##fav", fav ? Icon::StarFilled : Icon::Star, fav ? "Remove favorite" : "Favorite", fav)) s_.settings.set_favorite(def.id, !fav);
        if (hooked) {
            place(130 * S());
            if (success_button("Unhook", ImVec2(130 * S(), 40 * S()))) s_.unhook_current();
        } else {
            place(130 * S());
            if (primary_button(s_.hooking ? "Hooking..." : "Hook", ImVec2(130 * S(), 40 * S()), pid != 0 && !s_.hooking)) s_.hook_current();
            if (!pid) {
                place(130 * S());
                if (accent_soft_button("Play", ImVec2(130 * S(), 40 * S()))) s_.launch_current();
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + chh + 6 * S()));
        ImGui::Dummy(ImVec2(w, 4 * S()));
    }

    if (!s_.hook_message.empty()) {
        std::string m = s_.hook_message;
        banner(m.c_str(), s_.hook_message_bad ? col::bad : col::ok, s_.hook_message_bad ? rgb(0x2d1618) : col::ok_bg);
        ImGui::Dummy(ImVec2(0, 6 * S()));
    }
    if (!s_.build_warning.empty()) {
        std::string m = s_.build_warning;
        banner(m.c_str(), col::warn, col::warn_bg);
        ImGui::Dummy(ImVec2(0, 6 * S()));
    }
    if (!hooked && !pid) {
        banner("Start the game with Play, then Hooked turns on automatically. Every cheat needs the game hooked first.", col::dim, col::row);
        ImGui::Dummy(ImVec2(0, 6 * S()));
    }
    if (!def.notes.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::dim);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(def.notes.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4 * S()));
    }

    // tools row
    float sw = std::min(380.0f * S(), ImGui::GetContentRegionAvail().x * 0.5f);
    float row_y = ImGui::GetCursorPosY(), row_x = ImGui::GetCursorPosX();
    search_box("##csearch", search_, sizeof search_, "Search cheats", sw);
    float after_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPos(ImVec2(row_x + sw + 14 * S(), row_y));
    if (secondary_button("Save preset", ImVec2(0, 38 * S()), hooked)) s_.save_preset();
    ImGui::SameLine(0, 8 * S());
    bool has_preset = s_.settings.preset(def.id).has_value();
    if (secondary_button("Apply preset", ImVec2(0, 38 * S()), hooked && has_preset)) s_.apply_preset();
    ImGui::SameLine(0, 14 * S());
    bool aa = s_.settings.auto_apply(def.id);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6 * S());
    if (toggle_switch("##autoapply", &aa)) s_.settings.set_auto_apply(def.id, aa);
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4 * S());
    text_dim("Apply preset when hooked");
    ImGui::SetCursorPos(ImVec2(row_x, std::max(after_y, ImGui::GetCursorPosY())));
    ImGui::Dummy(ImVec2(0, 10 * S()));

    // cheats, pinned first, then by section in definition order
    std::vector<std::string> pins = s_.settings.pins(def.id);
    auto is_pinned = [&](const CheatDef& c) { return std::find(pins.begin(), pins.end(), c.id) != pins.end(); };
    std::vector<const CheatDef*> pinned;
    std::vector<std::string> sections;
    std::map<std::string, std::vector<const CheatDef*>> by_section;
    for (auto& c : def.cheats) {
        if (!contains_i(c.label + " " + c.info + " " + c.section, search_)) continue;
        if (is_pinned(c)) { pinned.push_back(&c); continue; }
        if (!by_section.count(c.section)) sections.push_back(c.section);
        by_section[c.section].push_back(&c);
    }
    auto draw_list = [&](const std::vector<const CheatDef*>& v) {
        int i = 0;
        for (auto* c : v) cheat_row(*c, def, (i++ & 1) == 0);
    };
    if (!pinned.empty()) {
        bool& col_ = collapsed_[def.id + "/#pins"];
        section_header("Pinned", Icon::PinFilled, &col_);
        if (!col_) draw_list(pinned);
    }
    for (auto& sec : sections) {
        bool& col_ = collapsed_[def.id + "/" + sec];
        section_header(sec.c_str(), Icon::Bolt, &col_);
        if (!col_) draw_list(by_section[sec]);
    }
    if (pinned.empty() && sections.empty()) text_dim("No cheats match your search.");

    bool open = capture_open_ && !capture_for_.empty() && capture_for_ != "#overlay";
    if (open) {
        const CheatDef* c = def.find_cheat(capture_for_);
        std::string title = "Hotkey for " + (c ? c->label : capture_for_);
        auto r = capture_hotkey_popup(title.c_str(), s_.settings.hotkey(def.id, capture_for_), &capture_open_);
        if (r) {
            if (r->empty() || parse_combo(*r)) {
                s_.settings.set_hotkey(def.id, capture_for_, *r);
                s_.rebuild_hotkeys();
            } else {
                s_.notify("That isn't a usable key combination.", true);
            }
        }
        if (!capture_open_) capture_for_.clear();
    }
}

// ==================================================================== scanner
void App::process_picker() {
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(fonts_.bold);
    ImGui::TextUnformatted("1. Pick the game process");
    ImGui::PopFont();
    std::optional<Session::Info> at;
    {
        std::lock_guard l(s_.mu);
        at = s_.attached;
    }
    if (at) {
        std::string t = "Attached to " + at->name + "  (PID " + std::to_string(at->pid) + ", " + (at->is64 ? "64-bit" : "32-bit") + ")";
        banner(t.c_str(), col::ok, col::ok_bg);
        if (secondary_button("Detach")) s_.detach();
        ImGui::Dummy(ImVec2(0, 8 * S()));
    } else {
        text_dim("Hexwright only touches the one process you attach to. Multiplayer and anti-cheat games are refused.");
    }
    if (!s_.attach_error.empty()) {
        std::string e = s_.attach_error;
        banner(e.c_str(), col::bad, rgb(0x2d1618));
    }
    float py = ImGui::GetCursorPosY(), px = ImGui::GetCursorPosX(), pw = std::min(w, 360.0f * S());
    search_box("##psearch", proc_search_, sizeof proc_search_, "Filter processes", pw);
    float py_after = ImGui::GetCursorPosY();
    ImGui::SetCursorPos(ImVec2(px + pw + 10 * S(), py));
    if (secondary_button("Refresh", ImVec2(0, 38 * S()))) s_.refresh_processes();
    ImGui::SetCursorPos(ImVec2(px, py_after + 6 * S()));
    {
        std::lock_guard l(s_.mu);
        if (s_.processes.empty()) { /* filled by Refresh */ }
    }
    static bool first = true;
    if (first) { first = false; s_.refresh_processes(); }
    ImGui::BeginChild("##plist", ImVec2(0, 170 * S()), ImGuiChildFlags_Borders, 0);
    std::vector<ProcessInfo> procs;
    {
        std::lock_guard l(s_.mu);
        procs = s_.processes;
    }
    for (auto& p : procs) {
        if (!contains_i(p.name, proc_search_) && !contains_i(std::to_string(p.pid), proc_search_)) continue;
        std::string label = p.name + "   " + std::to_string(p.pid);
        ImGui::PushID((int)p.pid);
        bool sel = at && at->pid == p.pid;
        if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 24 * S()))) {
            if (ImGui::IsMouseDoubleClicked(0) && !s_.attaching) s_.attach_pid(p.pid);
        }
        if (ImGui::BeginPopupContextItem("##pctx")) {
            if (ImGui::MenuItem("Attach") && !s_.attaching) s_.attach_pid(p.pid);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    text_dim("Double-click a process (or right-click, Attach) to attach.");
}

void App::scan_panel() {
    ScanView v = s_.scan_view();
    auto proc = s_.process();
    ImGui::PushFont(fonts_.bold);
    ImGui::TextUnformatted("2. Scan memory");
    ImGui::PopFont();
    const OpItem* ops = v.has_scan ? kNextOps : kFirstOps;
    int nops = v.has_scan ? (int)(sizeof kNextOps / sizeof kNextOps[0]) : (int)(sizeof kFirstOps / sizeof kFirstOps[0]);
    if (scan_op_ >= nops) scan_op_ = 0;
    ImGui::BeginDisabled(v.busy || !proc);
    ImGui::BeginDisabled(v.has_scan);
    combo_box("##type", &scan_type_, kTypeLabels, 6, 190 * S());
    ImGui::EndDisabled();
    ImGui::SameLine();
    std::vector<const char*> labels;
    for (int i = 0; i < nops; ++i) labels.push_back(ops[i].label);
    combo_box("##op", &scan_op_, labels.data(), nops, 220 * S());
    int need = ops[scan_op_].values;
    if (need >= 1) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140 * S());
        ImGui::InputTextWithHint("##v1", need == 2 ? "from" : "value", scan_v1_, sizeof scan_v1_);
    }
    if (need == 2) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140 * S());
        ImGui::InputTextWithHint("##v2", "to", scan_v2_, sizeof scan_v2_);
    }
    ValueType vt = kTypes[scan_type_];
    ScanParams sp;
    sp.op = ops[scan_op_].op;
    bool valid = true;
    if (need >= 1) {
        auto a = Value::parse(vt, scan_v1_);
        if (!a) valid = false; else sp.v1 = *a;
    }
    if (need == 2) {
        auto b = Value::parse(vt, scan_v2_);
        if (!b) valid = false; else sp.v2 = *b;
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 4 * S()));
    if (!v.has_scan) {
        if (primary_button("First scan", ImVec2(130 * S(), 36 * S()), !v.busy && proc && valid)) {
            s_.scan_cfg.type = vt;
            s_.first_scan(sp);
        }
    } else {
        if (primary_button("Next scan", ImVec2(130 * S(), 36 * S()), !v.busy && proc && valid)) s_.next_scan(sp);
        ImGui::SameLine();
        if (secondary_button("New scan", ImVec2(0, 36 * S()), !v.busy)) s_.reset_scan();
    }
    if (v.busy) {
        ImGui::SameLine();
        if (secondary_button("Cancel", ImVec2(0, 36 * S()))) s_.cancel_scan();
        ImGui::ProgressBar(v.progress, ImVec2(-1, 8 * S()), "");
    }
    if (!v.error.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, col::bad); ImGui::TextWrapped("%s", v.error.c_str()); ImGui::PopStyleColor(); }
    else if (v.has_scan && !v.busy) text_dim((v.status.empty() ? fmt_count(v.count) + " results" : v.status).c_str());
    if (!proc) text_dim("Attach to a process first.");

    // results
    if (v.has_scan && !v.busy && !v.snapshot) {
        int64_t now = now_ms();
        if (now - hits_at_ > 400 || hits_cache_.empty()) { hits_cache_ = s_.scan_results(300); hits_at_ = now; }
        ImGui::BeginChild("##hits", ImVec2(0, 190 * S()), ImGuiChildFlags_Borders, 0);
        ValueType rt = s_.scanner.config().type;
        for (size_t i = 0; i < hits_cache_.size(); ++i) {
            auto& h = hits_cache_[i];
            std::string cur = "?";
            if (proc) {
                uint8_t buf[8] = {};
                if (proc->read(h.address, buf, type_size(rt))) cur = Value::from_bytes(rt, buf).format();
            }
            std::string prev = Value{rt, h.previous}.format();
            char line[160];
            std::snprintf(line, sizeof line, "%-16s  now %-14s  was %s", format_hex(h.address, 8).c_str(), cur.c_str(), prev.c_str());
            ImGui::PushID((int)i);
            ImGui::PushFont(fonts_.mono);
            if (ImGui::Selectable(line, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(0) && proc) {
                    TableEntry e;
                    e.label = "Address " + format_hex(h.address);
                    e.type = rt;
                    // store module-relative so it survives restarts when the address is in a module
                    PointerChain pc;
                    pc.offsets = {(int64_t)h.address};
                    for (auto& m : proc->modules())
                        if (h.address >= m.base && h.address < m.base + m.size) { pc.module = m.name; pc.offsets = {(int64_t)(h.address - m.base)}; break; }
                    e.address = pc;
                    s_.table.add(e);
                }
            }
            ImGui::PopFont();
            ImGui::PopID();
        }
        ImGui::EndChild();
        text_dim("Double-click a result to add it to the address list.");
    }
}

void App::address_list() {
    auto proc = s_.process();
    ImGui::PushFont(fonts_.bold);
    ImGui::TextUnformatted("3. Address list");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(300 * S());
    ImGui::InputTextWithHint("##addchain", "module+offset,off1,off2  or absolute address", add_chain_, sizeof add_chain_);
    ImGui::SameLine();
    if (secondary_button("Add", ImVec2(0, 0), add_chain_[0] != 0)) {
        auto pc = PointerChain::parse(add_chain_);
        if (pc) {
            TableEntry e;
            e.label = "Pointer " + std::string(add_chain_);
            e.type = kTypes[scan_type_];
            e.address = *pc;
            s_.table.add(e);
            add_chain_[0] = 0;
        } else {
            s_.table_message = pc.message();
        }
    }
    ImGui::SameLine(0, 18 * S());
    if (secondary_button("Save table", ImVec2(0, 0), proc != nullptr)) s_.table_save();
    ImGui::SameLine();
    if (secondary_button("Load table", ImVec2(0, 0), proc != nullptr)) s_.table_load();
    if (!s_.table_message.empty()) text_dim(s_.table_message.c_str());

    auto entries = s_.table.snapshot();
    if (entries.empty()) { text_dim("Empty. Add results from a scan, or type a pointer chain above."); return; }
    int remove = -1;
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& e = entries[i];
        ImGui::PushID((int)i);
        float w = ImGui::GetContentRegionAvail().x;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + 46 * S()), u32((i & 1) ? col::panel : col::row), 9 * S());
        ImGui::SetCursorScreenPos(ImVec2(p.x + 12 * S(), p.y + 9 * S()));
        // freeze
        bool fz = e.frozen;
        if (toggle_switch("##fz", &fz)) {
            std::optional<Value> val;
            if (fz && proc) val = s_.table.read(*proc, i);
            s_.table.set_frozen(i, fz, val);
        }
        ImGui::SameLine(0, 12 * S());
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3 * S());
        std::string desc = e.label + "   " + e.address.format() + "   " + type_name(e.type);
        ImGui::PushFont(fonts_.mono);
        ImGui::TextUnformatted(desc.c_str());
        ImGui::PopFont();
        // value + edit at the right
        std::string err;
        std::optional<Value> cur = proc ? s_.table.read(*proc, i, &err) : std::nullopt;
        NumberField& f = table_fields_[i];
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - 330 * S(), p.y + 8 * S()));
        std::string cs = cur ? cur->format() : "?";
        ImGui::PushFont(fonts_.mono);
        ImGui::Text("%s", cs.c_str());
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - 220 * S(), p.y + 8 * S()));
        ImGui::SetNextItemWidth(120 * S());
        bool commit = ImGui::InputText("##edit", f.buf, sizeof f.buf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal);
        ImGui::SameLine();
        if ((secondary_button("Set") || commit) && proc) {
            if (auto nv = Value::parse(e.type, f.buf)) {
                std::string werr;
                if (!s_.table.write(*proc, i, *nv, &werr)) s_.table_message = werr;
                if (e.frozen) s_.table.set_frozen(i, true, nv);
            }
        }
        ImGui::SameLine();
        if (icon_button("##del", Icon::Trash, "Remove", false, 30.0f)) remove = (int)i;
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 52 * S()));
        ImGui::PopID();
    }
    if (remove >= 0) { s_.table.remove((size_t)remove); table_fields_.clear(); }
}

void App::aob_panel() {
    auto proc = s_.process();
    ImGui::PushFont(fonts_.bold);
    ImGui::TextUnformatted("AOB signature scan");
    ImGui::PopFont();
    text_dim("Find a byte pattern in the main module's code (wildcards: ?? or a single ?).");
    ImGui::SetNextItemWidth(420 * S());
    ImGui::InputTextWithHint("##aob", "48 8B 0D ?? ?? ?? ?? 41 B8", aob_text_, sizeof aob_text_);
    ImGui::SameLine();
    if (secondary_button("Scan .text", ImVec2(0, 0), proc && aob_text_[0])) {
        auto pat = Pattern::parse(aob_text_);
        if (!pat) aob_result_ = pat.message();
        else if (auto mm = proc->main_module()) {
            auto r = scan_module(*proc, *mm, ".text", *pat, 16);
            if (!r.ok()) aob_result_ = r.message();
            else if (r->empty()) aob_result_ = "No matches.";
            else {
                aob_result_ = std::to_string(r->size()) + " match(es):";
                for (auto a : *r) aob_result_ += "  " + mm->name + "+" + format_hex(a - mm->base);
            }
        }
    }
    if (!aob_result_.empty()) text_dim(aob_result_.c_str());
}

void App::page_scanner() {
    page_title("Scanner", "Find values in memory, freeze or edit them, and save the list per game.");
    process_picker();
    ImGui::Dummy(ImVec2(0, 14 * S()));
    scan_panel();
    ImGui::Dummy(ImVec2(0, 14 * S()));
    address_list();
    ImGui::Dummy(ImVec2(0, 14 * S()));
    aob_panel();
    ImGui::Dummy(ImVec2(0, 30 * S()));
}

// =================================================================== settings
void App::page_settings() {
    page_title("Settings");
    AppSettings a = s_.settings.app();
    bool changed = false;
    auto row = [&](const char* label, const char* hint, bool* v) {
        ImGui::PushID(label);
        if (toggle_switch("##t", v)) changed = true;
        ImGui::SameLine(0, 14 * S());
        ImGui::BeginGroup();
        ImGui::TextUnformatted(label);
        ImGui::PushStyleColor(ImGuiCol_Text, col::dimmer);
        ImGui::TextUnformatted(hint);
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, 6 * S()));
        ImGui::PopID();
    };
    ImGui::PushFont(fonts_.bold);
    ImGui::TextUnformatted("General");
    ImGui::PopFont();
    row("Hook automatically", "Attach to the selected game as soon as it starts.", &a.auto_hook);
    row("Global hotkeys", "Cheat hotkeys work while the game has focus.", &a.hotkeys_enabled);
    bool sw = a.start_with_windows;
    row("Start with Windows", "Launch minimized when you sign in.", &a.start_with_windows);
    if (a.start_with_windows != sw) run_at_startup(a.start_with_windows);
    row("Check for updates automatically", "Looks at GitHub Releases when Hexwright starts.", &a.autoupdate);
    row("Sync game definitions", "Download new and updated trainers for the games you own.", &a.catalog_sync);

    ImGui::Dummy(ImVec2(0, 8 * S()));
    ImGui::PushFont(fonts_.bold);
    ImGui::TextUnformatted("Overlay and console");
    ImGui::PopFont();
    ImGui::Text("Overlay hotkey");
    ImGui::SameLine(0, 14 * S());
    if (secondary_button((a.overlay_hotkey.empty() ? "Not set" : a.overlay_hotkey.c_str()), ImVec2(180 * S(), 0))) { capture_for_ = "#overlay"; capture_open_ = true; }
    ImGui::Text("Console key");
    ImGui::SameLine(0, 14 * S());
    bool combo = a.console_mode == "combo";
    int idx = combo ? 1 : 0;
    const char* modes[] = {"Backtick (`)", "Shift+Alt+C"};
    if (combo_box("##cm", &idx, modes, 2, 180 * S())) { a.console_mode = idx ? "combo" : "key"; changed = true; }
    text_dim("Use Shift+Alt+C if your keyboard layout has no backtick key.");
    if (changed) {
        s_.settings.set_app(a);
        s_.rebuild_hotkeys();
    }
    if (capture_open_ && capture_for_ == "#overlay") {
        auto r = capture_hotkey_popup("Overlay hotkey", a.overlay_hotkey, &capture_open_);
        if (r && (r->empty() || parse_combo(*r))) {
            a.overlay_hotkey = *r;
            s_.settings.set_app(a);
            s_.rebuild_hotkeys();
        }
        if (!capture_open_) capture_for_.clear();
    }

    ImGui::Dummy(ImVec2(0, 14 * S()));
    ImGui::PushFont(fonts_.bold);
    ImGui::TextUnformatted("Library and catalog");
    ImGui::PopFont();
    {
        std::lock_guard l(s_.mu);
        std::string t = std::to_string(s_.detected.size()) + " games detected. " + s_.catalog_status;
        text_dim(t.c_str());
    }
    if (secondary_button("Rescan library", ImVec2(0, 0), !s_.scanning_library)) s_.rescan_library(true);
    ImGui::SameLine();
    if (secondary_button("Sync catalog", ImVec2(0, 0), !s_.syncing_catalog)) s_.sync_catalog();

    ImGui::Dummy(ImVec2(0, 14 * S()));
    ImGui::PushFont(fonts_.bold);
    ImGui::TextUnformatted("Updates");
    ImGui::PopFont();
    {
        std::lock_guard l(s_.mu);
        text_dim(s_.update_status.empty() ? "Not checked yet." : s_.update_status.c_str());
    }
    if (secondary_button("Check now", ImVec2(0, 0), !s_.update_busy)) s_.check_update(false);
    bool have;
    {
        std::lock_guard l(s_.mu);
        have = s_.update.has_value();
    }
    if (have) {
        ImGui::SameLine();
        if (primary_button("Install update", ImVec2(0, 0), !s_.update_busy)) s_.apply_update_now();
    }

    ImGui::Dummy(ImVec2(0, 14 * S()));
    ImGui::PushFont(fonts_.bold);
    ImGui::TextUnformatted("Safety");
    ImGui::PopFont();
    banner("Hexwright is for single-player games. It refuses to attach when a multiplayer game or an anti-cheat "
           "(EasyAntiCheat, BattlEye, Vanguard, FACEIT, Ricochet and others) is running, and it only ever touches the one process you pick. "
           "There is no override.",
           col::dim, col::row);
    if (ImGui::TreeNode("Blocked anti-cheat and games")) {
        for (auto& n : anticheat_process_names()) ImGui::BulletText("%s", n.c_str());
        for (auto& n : blocked_game_exes()) ImGui::BulletText("%s", n.c_str());
        ImGui::TreePop();
    }
    ImGui::Dummy(ImVec2(0, 30 * S()));
}

// ==================================================================== overlay
void App::draw_overlay(const Fonts& fonts) {
    fonts_ = fonts;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * S(), 14 * S()));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.045f, 0.06f, 1.0f));
    ImGui::Begin("##overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    const LoadedGame* g = s_.current();
    ImGui::PushFont(fonts_.bold);
    ImGui::PushStyleColor(ImGuiCol_Text, col::accent);
    ImGui::TextUnformatted(g ? g->def.name.c_str() : "Hexwright");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    auto active = s_.active_cheat_labels();
    if (active.empty()) text_dim("No cheats active");
    for (auto& l : active) {
        ImGui::PushStyleColor(ImGuiCol_Text, col::ok);
        ImGui::TextUnformatted("•");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted(l.c_str());
    }
    ImGui::End();
}

}  // namespace hx::ui
