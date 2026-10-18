#include "core/games.h"

#include <algorithm>

#include "core/bundled.h"
#include "core/catalog.h"
#include "core/hash.h"
#include "hx/version.h"

namespace hx {

GameStore::Config GameStore::default_config() {
    Config c;
    c.catalog_dir = data_dir() / "catalog" / "games";
    c.user_dir = data_dir() / "games";
    c.app_version = HX_VERSION_STRING;
    return c;
}

const LoadedGame* GameStore::find(const std::string& id) const {
    for (auto& g : games_) if (g.def.id == id) return &g;
    return nullptr;
}

void GameStore::reload() {
    std::vector<LoadedGame> out;
    auto put = [&](Result<GameDef> def, const std::string& source, const std::string& hash, const std::string& what) {
        if (!def.ok()) { log_warn("skipping definition " + what + ": " + def.message()); return; }
        if (!def->min_app.empty() && compare_versions(def->min_app, cfg_.app_version) > 0) {
            log_info("skipping " + what + ": needs Hexwright " + def->min_app + "+");
            return;
        }
        LoadedGame lg{std::move(*def), source, hash};
        auto it = std::find_if(out.begin(), out.end(), [&](const LoadedGame& g) { return g.def.id == lg.def.id; });
        if (it == out.end()) out.push_back(std::move(lg));
        else *it = std::move(lg);
    };
    auto from_dir = [&](const fs::path& dir, const std::string& source) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return;
        std::vector<fs::path> files;
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".json" && e.path().filename().string()[0] != '_') files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (auto& f : files) {
            auto text = read_file(f, kMaxDefinitionBytes + 1);
            if (!text) { log_warn("cannot read " + narrow(f.wstring())); continue; }
            put(parse_game_text(*text), source, sha256_normalized(*text), narrow(f.filename().wstring()));
        }
    };
    if (cfg_.use_bundled)
        for (auto& b : bundled_definitions())
            put(parse_game_text(std::string_view(b.data, b.size)), "bundled", sha256_normalized(std::string_view(b.data, b.size)), b.name);
    from_dir(cfg_.catalog_dir, "catalog");
    from_dir(cfg_.user_dir, "user");
    games_ = std::move(out);
}

}  // namespace hx
