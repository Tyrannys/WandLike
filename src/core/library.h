#pragma once
// Finds installed games in Steam, Epic and GOG. Manifests and directory listings are cached, so a normal rescan
// only stat()s a handful of paths and never re-reads an unchanged file.
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/util.h"

namespace hx {

struct LibraryGame {
    std::string store;          // "steam" | "epic" | "gog"
    std::string id;             // Steam appid / Epic AppName / GOG product id
    std::string name;
    fs::path install_dir;
    std::string exe;            // Epic/GOG record the launch executable
};

struct ScanStats {
    int files_parsed = 0, files_cached = 0, dirs_listed = 0, dirs_cached = 0;
};

class Library {
public:
    struct Sources {
        std::function<std::optional<fs::path>()> steam_root;     // default: registry
        fs::path epic_manifests;                                 // default: ProgramData\Epic\...\Manifests
        bool scan_gog = true;
    };
    Library(fs::path cache_file, Sources src);
    static Library with_defaults();

    // `force` drops the cache first and re-reads everything.
    std::vector<LibraryGame> scan(bool force = false);
    [[nodiscard]] const std::vector<LibraryGame>& games() const { return games_; }
    [[nodiscard]] const ScanStats& stats() const { return stats_; }
    [[nodiscard]] int64_t scanned_at() const { return scanned_at_; }

private:
    struct FileEntry { int64_t mtime = 0; uint64_t size = 0; nlohmann::json data; };
    struct DirEntry { int64_t mtime = 0; std::vector<std::string> names; };

    std::optional<nlohmann::json> parsed(const fs::path& p, const std::function<nlohmann::json(const std::string&)>& parse);
    std::vector<std::string> listing(const fs::path& dir, const std::string& prefix, const std::string& suffix);
    std::vector<LibraryGame> scan_steam();
    std::vector<LibraryGame> scan_epic();
    std::vector<LibraryGame> scan_gog();
    void load_cache();
    void save_cache();

    fs::path cache_file_;
    Sources src_;
    std::map<std::string, FileEntry> files_;
    std::map<std::string, DirEntry> dirs_;
    std::map<std::string, bool> seen_files_, seen_dirs_;
    std::vector<LibraryGame> games_;
    ScanStats stats_;
    int64_t scanned_at_ = 0;
};

// The detected game that satisfies a definition's `match` ({"steam_appid": N} / {"epic": id} / {"gog": id}).
const LibraryGame* find_game(const std::vector<LibraryGame>& games, const nlohmann::json& match);

// Local Steam artwork (cover, hero, logo) from Steam's own cache; empty path if Steam has none.
fs::path steam_art_path(const std::string& appid, const std::string& kind);   // kind: cover | hero | logo | header

}  // namespace hx
