#pragma once
// Persistent per-user settings (%APPDATA%\Hexwright\settings.json). Same JSON layout the earlier builds used,
// so existing settings carry over: {"game": id, "app": {...}, "g.<gameid>": {...}}.
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/util.h"

namespace hx {

struct AppSettings {
    bool autoupdate = true;
    bool start_with_windows = false;
    std::string overlay_hotkey = "Ctrl+Shift+F10";
    bool hotkeys_enabled = true;
    std::string console_mode = "key";       // "key" (backtick) or "combo" (Shift+Alt+C)
    bool catalog_sync = true;
    bool auto_hook = true;
};

struct Preset {
    std::vector<std::string> toggles;                         // toggles that were on
    std::map<std::string, std::pair<double, std::string>> numbers;   // cheat id -> (value, mode)
};

class Settings {
public:
    explicit Settings(fs::path path = {});

    [[nodiscard]] AppSettings app() const;
    void set_app(const AppSettings& a);

    [[nodiscard]] std::string last_game() const;
    void set_last_game(const std::string& id);

    // Per-game data
    [[nodiscard]] std::vector<std::string> pins(const std::string& game) const;
    void set_pinned(const std::string& game, const std::string& cheat, bool pinned);
    [[nodiscard]] std::string hotkey(const std::string& game, const std::string& cheat) const;
    [[nodiscard]] std::map<std::string, std::string> hotkeys(const std::string& game) const;
    void set_hotkey(const std::string& game, const std::string& cheat, const std::string& combo);   // "" clears
    [[nodiscard]] double value(const std::string& game, const std::string& cheat, double def) const;
    [[nodiscard]] std::string mode(const std::string& game, const std::string& cheat) const;
    void remember_value(const std::string& game, const std::string& cheat, double value, const std::string& mode);
    [[nodiscard]] double option(const std::string& game, const std::string& cheat, const std::string& option, double def) const;
    void set_option(const std::string& game, const std::string& cheat, const std::string& option, double value);
    [[nodiscard]] std::optional<Preset> preset(const std::string& game) const;
    void set_preset(const std::string& game, const Preset& p);
    [[nodiscard]] bool favorite(const std::string& game) const;
    void set_favorite(const std::string& game, bool v);
    [[nodiscard]] bool auto_apply(const std::string& game) const;
    void set_auto_apply(const std::string& game, bool v);
    [[nodiscard]] int64_t last_played(const std::string& game) const;
    void set_last_played(const std::string& game, int64_t unix_seconds);

private:
    nlohmann::json game_locked(const std::string& id) const;
    void put_game_locked(const std::string& id, nlohmann::json g);
    void save_locked();

    fs::path path_;
    mutable std::mutex mu_;
    nlohmann::json data_;
};

}  // namespace hx
