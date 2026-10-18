#pragma once
// The Hexwright interface: sidebar + pages. One instance draws into the main window; draw_overlay() into the overlay window.
#include <map>
#include <string>

#include "app/state.h"
#include "app/widgets.h"

namespace hx::ui {

enum class Page { Home, Games, Explore, Game, Scanner, Settings };

class App {
public:
    explicit App(AppState& s) : s_(s) {}
    void draw(const Fonts& fonts);
    void draw_overlay(const Fonts& fonts);

private:
    // pages
    void sidebar();
    void page_home();
    void page_games();
    void page_explore();
    void page_game();
    void page_scanner();
    void page_settings();
    void toasts();
    // building blocks
    void page_title(const char* title, const char* subtitle = nullptr);
    bool game_card(const LoadedGame& g, ImVec2 size);
    void cheat_row(const CheatDef& c, const GameDef& def, bool even);
    void process_picker();
    void scan_panel();
    void address_list();
    void aob_panel();
    const Texture* cover_for(const LoadedGame& g);

    AppState& s_;
    Fonts fonts_;
    Page page_ = Page::Home;
    char search_[96] = "";
    char proc_search_[64] = "";
    std::map<std::string, NumberField> fields_;           // per cheat id
    std::map<std::string, bool> collapsed_;
    std::map<std::string, std::string> modes_;
    std::string capture_for_;                             // cheat id whose hotkey is being captured ("" = none; "#overlay" = overlay key)
    bool capture_open_ = false;
    std::string last_game_;
    // scanner page state
    int scan_type_ = 2;                                   // index into kTypes
    int scan_op_ = 0;
    char scan_v1_[40] = "";
    char scan_v2_[40] = "";
    char add_chain_[96] = "";
    char aob_text_[160] = "";
    std::string aob_result_;
    std::map<std::string, fs::path> art_;
    std::vector<Hit> hits_cache_;
    int64_t hits_at_ = 0;
    std::map<size_t, NumberField> table_fields_;
    std::string sel_value_;
};

}  // namespace hx::ui
