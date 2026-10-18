#pragma once
// Game definitions live in the GitHub repo. Updates are detected by CONTENT (SHA-256), not by a version number:
// when a detected game's file differs from what we have (bundled or downloaded), it is pulled, verified and swapped in.
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/http.h"
#include "core/library.h"
#include "core/util.h"

namespace hx {

struct CatalogEntry {
    std::string id, name, file, sha256, min_app;
    nlohmann::json match;
    int cheats = 0;
};

struct UiCatalogEntry {
    std::string id, name;
    int cheats = 0;
    bool installed = false;
};

int compare_versions(const std::string& a, const std::string& b);        // "2.0.0" vs "1.10.3"; <0, 0, >0

class Catalog {
public:
    struct Config {
        std::string repo;                                  // "owner/repo"
        std::string branch;
        std::string path;                                  // folder inside the repo
        fs::path dir;                                      // local cache: index.json, index.etag, games/*.json
        std::string app_version;
        HttpFetch fetch;                                   // injectable for tests
        std::function<std::optional<std::string>(const std::string& file)> bundled_hash;   // SHA-256 (normalized) of a bundled file
    };
    static Config default_config();

    explicit Catalog(Config cfg);

    bool fetch_index();                                    // true if the index changed; problems land in status()
    // Downloads definitions for detected games whose content changed. Returns messages
    // ("Added support for X", "Updated support for X", or "! problem").
    std::vector<std::string> sync(const std::vector<LibraryGame>& detected);

    [[nodiscard]] const std::vector<CatalogEntry>& index() const { return index_; }
    [[nodiscard]] const std::string& status() const { return status_; }
    [[nodiscard]] int64_t last_sync() const { return last_sync_; }
    [[nodiscard]] fs::path games_dir() const { return cfg_.dir / "games"; }
    [[nodiscard]] std::vector<UiCatalogEntry> entries_for_ui(const std::set<std::string>& loaded_ids,
                                                             const std::vector<LibraryGame>& detected) const;

private:
    std::string url(const std::string& file) const;
    void reset_if_app_changed();
    std::optional<std::string> download(const CatalogEntry& e, bool updated);

    Config cfg_;
    std::vector<CatalogEntry> index_;
    std::string status_ = "not synced yet";
    int64_t last_sync_ = 0;
};

}  // namespace hx
