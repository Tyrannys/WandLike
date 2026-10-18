#pragma once
// All game definitions the app knows: bundled in the exe, downloaded from the catalog, or dropped in by the user.
#include <string>
#include <vector>

#include "core/definition.h"

namespace hx {

struct LoadedGame {
    GameDef def;
    std::string source;         // "bundled" | "catalog" | "user"
    std::string file_hash;      // SHA-256 (normalized) of the file the definition was read from
};

class GameStore {
public:
    struct Config {
        fs::path catalog_dir;                 // downloaded definitions
        fs::path user_dir;                    // %APPDATA%\Hexwright\games
        std::string app_version;
        bool use_bundled = true;
    };
    static Config default_config();

    explicit GameStore(Config cfg) : cfg_(std::move(cfg)) { reload(); }
    // Bundled first, then downloaded, then the user's own: each later source replaces the earlier one for the same id.
    // Files that fail to parse, are too big or need a newer app are skipped and logged.
    void reload();
    [[nodiscard]] const std::vector<LoadedGame>& games() const { return games_; }
    [[nodiscard]] const LoadedGame* find(const std::string& id) const;

private:
    Config cfg_;
    std::vector<LoadedGame> games_;
};

}  // namespace hx
