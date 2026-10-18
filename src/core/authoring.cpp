#include "core/authoring.h"

#include <algorithm>
#include <regex>

#include "core/definition.h"
#include "core/hash.h"

namespace hx {

using nlohmann::json;

std::string slugify(const std::string& name) {
    std::string s;
    for (char c : lower(name)) {
        if (std::isalnum((unsigned char)c)) s.push_back(c);
        else if (!s.empty() && s.back() != '-') s.push_back('-');
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    if (s.size() > 48) s.resize(48);
    return s.empty() ? "game" : s;
}

std::string guess_exe(const LibraryGame& g) {
    if (!g.exe.empty()) return narrow(fs::path(widen(g.exe)).filename().wstring());
    static const std::regex skip("unins|redist|crash|setup|install|dxweb|vc_|launcher|helper|updater|eac|easyanticheat|bootstrap", std::regex::icase);
    std::vector<std::pair<uint64_t, std::string>> found;
    std::error_code ec;
    for (const char* sub : {"", "bin", "bin64", "binaries", "x64"}) {
        fs::path dir = g.install_dir / sub;
        for (auto& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec) || lower(narrow(e.path().extension().wstring())) != ".exe") continue;
            std::string name = narrow(e.path().filename().wstring());
            if (std::regex_search(name, skip)) continue;
            found.emplace_back((uint64_t)e.file_size(ec), name);
        }
    }
    if (found.empty()) return "game.exe";
    return std::max_element(found.begin(), found.end())->second;
}

json scaffold_definition(const LibraryGame& g) {
    json j;
    j["id"] = slugify(g.name);
    j["name"] = g.name;
    j["version"] = 1;
    if (g.store == "steam") {
        j["match"] = json{{"steam_appid", std::atoll(g.id.c_str())}};
        j["steam_appid"] = std::atoll(g.id.c_str());
    } else {
        j["match"] = json{{g.store, g.id}};
    }
    j["exe"] = guess_exe(g);
    j["window_title"] = g.name;
    j["launch_args"] = "";
    j["console"] = json{{"toggle_scancode", 41}, {"open_delay", 0.25}, {"line_delay", 0.15}, {"close_after", true}};
    j["notes"] = "Describe how this definition drives the game (console key, launch option needed, limitations).";
    j["cheats"] = json::array({
        json{{"id", "gold"}, {"label", "Set Gold"}, {"kind", "number"}, {"info", "Replace with a real console command."},
             {"set", "setgold {value}"}, {"default", 1000}, {"step", 1000}, {"verified", "inferred"}},
        json{{"id", "god"}, {"label", "God Mode"}, {"kind", "toggle"}, {"info", "Replace with real commands."},
             {"on", json::array({"god"})}, {"off", json::array({"god"})}, {"verified", "inferred"}}});
    return j;
}

std::string build_index_text(const fs::path& games_dir, std::string* error) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(games_dir, ec))
        if (e.path().extension() == ".json" && e.path().filename().string()[0] != '_') files.push_back(e.path());
    std::sort(files.begin(), files.end());
    json games = json::array();
    for (auto& f : files) {
        auto text = read_file(f, kMaxDefinitionBytes + 1);
        if (!text) { if (error) *error = "cannot read " + narrow(f.filename().wstring()); return ""; }
        auto g = parse_game_text(*text);
        if (!g.ok()) { if (error) *error = narrow(f.filename().wstring()) + ": " + g.message(); return ""; }
        if (narrow(f.stem().wstring()) != g->id) { if (error) *error = narrow(f.filename().wstring()) + ": the file name must equal the id (" + g->id + ")"; return ""; }
        int cheats = 0;
        for (auto& c : g->cheats) if (c.available() && c.kind != CheatKind::Button) ++cheats;
        json e = {{"id", g->id}, {"name", g->name}, {"version", g->version}, {"file", narrow(f.filename().wstring())},
                  {"sha256", sha256_normalized(*text)}, {"match", g->match_spec()}, {"cheats", cheats}};
        if (!g->min_app.empty()) e["min_app"] = g->min_app;
        games.push_back(std::move(e));
    }
    return json{{"schema", 1}, {"games", games}}.dump(2) + "\n";
}

}  // namespace hx
