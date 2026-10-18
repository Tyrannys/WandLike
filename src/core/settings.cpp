#include "core/settings.h"

#include <algorithm>

namespace hx {

using nlohmann::json;

Settings::Settings(fs::path path) : path_(path.empty() ? data_dir() / "settings.json" : std::move(path)), data_(json::object()) {
    if (auto text = read_file(path_, 4u << 20)) {
        json j = json::parse(*text, nullptr, false);
        if (j.is_object()) data_ = std::move(j);
    }
}

void Settings::save_locked() { write_file_atomic(path_, data_.dump(2)); }

AppSettings Settings::app() const {
    std::lock_guard l(mu_);
    AppSettings a;
    if (!data_.contains("app") || !data_["app"].is_object()) return a;
    const json& j = data_["app"];
    auto b = [&](const char* k, bool& out) { if (j.contains(k) && j[k].is_boolean()) out = j[k].get<bool>(); };
    auto s = [&](const char* k, std::string& out) { if (j.contains(k) && j[k].is_string()) out = j[k].get<std::string>(); };
    b("autoupdate", a.autoupdate); b("start_with_windows", a.start_with_windows); b("hotkeys_enabled", a.hotkeys_enabled);
    b("catalog_sync", a.catalog_sync); b("auto_hook", a.auto_hook);
    s("overlay_hotkey", a.overlay_hotkey); s("console_mode", a.console_mode);
    return a;
}

void Settings::set_app(const AppSettings& a) {
    std::lock_guard l(mu_);
    json j = data_.contains("app") && data_["app"].is_object() ? data_["app"] : json::object();   // keep keys we do not know
    j["autoupdate"] = a.autoupdate;
    j["start_with_windows"] = a.start_with_windows;
    j["overlay_hotkey"] = a.overlay_hotkey;
    j["hotkeys_enabled"] = a.hotkeys_enabled;
    j["console_mode"] = a.console_mode;
    j["catalog_sync"] = a.catalog_sync;
    j["auto_hook"] = a.auto_hook;
    data_["app"] = std::move(j);
    save_locked();
}

std::string Settings::last_game() const {
    std::lock_guard l(mu_);
    return data_.value("game", "");
}

void Settings::set_last_game(const std::string& id) {
    std::lock_guard l(mu_);
    data_["game"] = id;
    save_locked();
}

json Settings::game_locked(const std::string& id) const {
    auto it = data_.find("g." + id);
    return it != data_.end() && it->is_object() ? *it : json::object();
}

void Settings::put_game_locked(const std::string& id, json g) {
    data_["g." + id] = std::move(g);
    save_locked();
}

std::vector<std::string> Settings::pins(const std::string& game) const {
    std::lock_guard l(mu_);
    std::vector<std::string> out;
    json g = game_locked(game);
    if (g.contains("pins") && g["pins"].is_array())
        for (auto& p : g["pins"]) if (p.is_string()) out.push_back(p.get<std::string>());
    return out;
}

void Settings::set_pinned(const std::string& game, const std::string& cheat, bool pinned) {
    std::lock_guard l(mu_);
    json g = game_locked(game);
    std::vector<std::string> pins;
    if (g.contains("pins") && g["pins"].is_array())
        for (auto& p : g["pins"]) if (p.is_string() && p.get<std::string>() != cheat) pins.push_back(p.get<std::string>());
    if (pinned) pins.push_back(cheat);
    std::sort(pins.begin(), pins.end());
    g["pins"] = pins;
    put_game_locked(game, std::move(g));
}

std::map<std::string, std::string> Settings::hotkeys(const std::string& game) const {
    std::lock_guard l(mu_);
    std::map<std::string, std::string> out;
    json g = game_locked(game);
    if (g.contains("hotkeys") && g["hotkeys"].is_object())
        for (auto it = g["hotkeys"].begin(); it != g["hotkeys"].end(); ++it) if (it->is_string()) out[it.key()] = it->get<std::string>();
    return out;
}

std::string Settings::hotkey(const std::string& game, const std::string& cheat) const {
    auto h = hotkeys(game);
    auto it = h.find(cheat);
    return it == h.end() ? "" : it->second;
}

void Settings::set_hotkey(const std::string& game, const std::string& cheat, const std::string& combo) {
    std::lock_guard l(mu_);
    json g = game_locked(game);
    if (!g.contains("hotkeys") || !g["hotkeys"].is_object()) g["hotkeys"] = json::object();
    if (combo.empty()) g["hotkeys"].erase(cheat);
    else g["hotkeys"][cheat] = combo;
    put_game_locked(game, std::move(g));
}

double Settings::value(const std::string& game, const std::string& cheat, double def) const {
    std::lock_guard l(mu_);
    json g = game_locked(game);
    if (g.contains("values") && g["values"].is_object() && g["values"].contains(cheat) && g["values"][cheat].is_number()) return g["values"][cheat].get<double>();
    return def;
}

std::string Settings::mode(const std::string& game, const std::string& cheat) const {
    std::lock_guard l(mu_);
    json g = game_locked(game);
    if (g.contains("modes") && g["modes"].is_object() && g["modes"].contains(cheat) && g["modes"][cheat].is_string()) return g["modes"][cheat].get<std::string>();
    return "set";
}

void Settings::remember_value(const std::string& game, const std::string& cheat, double value, const std::string& mode) {
    std::lock_guard l(mu_);
    json g = game_locked(game);
    g["values"][cheat] = value;
    g["modes"][cheat] = mode;
    put_game_locked(game, std::move(g));
}

double Settings::option(const std::string& game, const std::string& cheat, const std::string& option, double def) const {
    std::lock_guard l(mu_);
    json g = game_locked(game);
    if (g.contains("options") && g["options"].contains(cheat) && g["options"][cheat].contains(option) && g["options"][cheat][option].is_number())
        return g["options"][cheat][option].get<double>();
    return def;
}

void Settings::set_option(const std::string& game, const std::string& cheat, const std::string& option, double value) {
    std::lock_guard l(mu_);
    json g = game_locked(game);
    g["options"][cheat][option] = value;
    put_game_locked(game, std::move(g));
}

std::optional<Preset> Settings::preset(const std::string& game) const {
    std::lock_guard l(mu_);
    json g = game_locked(game);
    if (!g.contains("preset") || !g["preset"].is_object()) return std::nullopt;
    Preset p;
    const json& j = g["preset"];
    if (j.contains("toggles") && j["toggles"].is_array())
        for (auto& t : j["toggles"]) if (t.is_string()) p.toggles.push_back(t.get<std::string>());
    if (j.contains("numbers") && j["numbers"].is_object())
        for (auto it = j["numbers"].begin(); it != j["numbers"].end(); ++it)
            if (it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_string())
                p.numbers[it.key()] = {(*it)[0].get<double>(), (*it)[1].get<std::string>()};
    return p;
}

void Settings::set_preset(const std::string& game, const Preset& p) {
    std::lock_guard l(mu_);
    json g = game_locked(game);
    json j;
    j["toggles"] = p.toggles;
    j["numbers"] = json::object();
    for (auto& [id, vm] : p.numbers) j["numbers"][id] = json::array({vm.first, vm.second});
    g["preset"] = std::move(j);
    put_game_locked(game, std::move(g));
}

bool Settings::favorite(const std::string& game) const { std::lock_guard l(mu_); return game_locked(game).value("favorite", false); }
void Settings::set_favorite(const std::string& game, bool v) {
    std::lock_guard l(mu_);
    json g = game_locked(game);
    g["favorite"] = v;
    put_game_locked(game, std::move(g));
}
bool Settings::auto_apply(const std::string& game) const { std::lock_guard l(mu_); return game_locked(game).value("auto_apply", false); }
void Settings::set_auto_apply(const std::string& game, bool v) {
    std::lock_guard l(mu_);
    json g = game_locked(game);
    g["auto_apply"] = v;
    put_game_locked(game, std::move(g));
}
int64_t Settings::last_played(const std::string& game) const { std::lock_guard l(mu_); return game_locked(game).value("last_played", (int64_t)0); }
void Settings::set_last_played(const std::string& game, int64_t t) {
    std::lock_guard l(mu_);
    json g = game_locked(game);
    g["last_played"] = t;
    put_game_locked(game, std::move(g));
}

}  // namespace hx
