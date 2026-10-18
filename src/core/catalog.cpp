#include "core/catalog.h"

#include <regex>

#include "core/bundled.h"
#include "core/definition.h"
#include "core/hash.h"
#include "hx/version.h"

namespace hx {

using nlohmann::json;

namespace {
constexpr size_t kMaxIndexBytes = 512 * 1024;

std::vector<int> parts(const std::string& v) {
    std::vector<int> out;
    std::string cur;
    for (char c : v + ".") {
        if (std::isdigit((unsigned char)c)) cur.push_back(c);
        else if (c == '.' ) { out.push_back(cur.empty() ? 0 : std::atoi(cur.c_str())); cur.clear(); }
        else if (!cur.empty()) { out.push_back(std::atoi(cur.c_str())); cur.clear(); break; }
    }
    return out;
}
}  // namespace

int compare_versions(const std::string& a, const std::string& b) {
    auto pa = parts(a[0] == 'v' || a[0] == 'V' ? a.substr(1) : a), pb = parts(b[0] == 'v' || b[0] == 'V' ? b.substr(1) : b);
    size_t n = std::max(pa.size(), pb.size());
    pa.resize(n);
    pb.resize(n);
    for (size_t i = 0; i < n; ++i)
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    return 0;
}

Catalog::Config Catalog::default_config() {
    Config c;
    c.repo = HX_UPDATE_REPO;
    c.branch = HX_CATALOG_BRANCH;
    c.path = HX_CATALOG_PATH;
    c.dir = data_dir() / "catalog";
    c.app_version = HX_VERSION_STRING;
    c.fetch = [](const std::string& url, const HttpOptions& o) { return http_get(url, o); };
    c.bundled_hash = [](const std::string& file) -> std::optional<std::string> {
        for (auto& b : bundled_definitions())
            if (file == b.name) return sha256_normalized(std::string_view(b.data, b.size));
        return std::nullopt;
    };
    return c;
}

Catalog::Catalog(Config cfg) : cfg_(std::move(cfg)) {
    std::error_code ec;
    fs::create_directories(games_dir(), ec);
    reset_if_app_changed();
    if (auto text = read_file(cfg_.dir / "index.json", kMaxIndexBytes)) {
        json j = json::parse(*text, nullptr, false);
        if (j.is_object() && j.contains("games") && j["games"].is_array())
            for (auto& g : j["games"]) {
                if (!g.is_object() || !g.contains("id") || !g.contains("file") || !g.contains("sha256")) continue;
                CatalogEntry e;
                e.id = g.value("id", "");
                e.name = g.value("name", e.id);
                e.file = g.value("file", "");
                e.sha256 = lower(g.value("sha256", ""));
                e.min_app = g.value("min_app", "");
                e.match = g.value("match", json::object());
                e.cheats = g.value("cheats", 0);
                index_.push_back(std::move(e));
            }
    }
}

std::string Catalog::url(const std::string& file) const {
    return "https://raw.githubusercontent.com/" + cfg_.repo + "/" + cfg_.branch + "/" + cfg_.path + "/" + file;
}

void Catalog::reset_if_app_changed() {
    fs::path marker = cfg_.dir / "app_version.txt";
    auto cur = read_file(marker, 64);
    if (cur && trim(*cur) == cfg_.app_version) return;
    // A new app version ships its own bundled definitions: drop downloaded copies and re-pull only what is newer.
    std::error_code ec;
    for (auto& e : fs::directory_iterator(games_dir(), ec))
        if (e.path().extension() == ".json") fs::remove(e.path(), ec);
    fs::remove(cfg_.dir / "index.etag", ec);
    write_file_atomic(marker, cfg_.app_version);
}

bool Catalog::fetch_index() {
    HttpOptions opt;
    opt.max_bytes = kMaxIndexBytes;
    if (!index_.empty())
        if (auto etag = read_file(cfg_.dir / "index.etag", 256)) opt.headers["If-None-Match"] = trim(*etag);
    HttpResponse r = cfg_.fetch(url("_index.json"), opt);
    last_sync_ = unix_seconds();
    if (r.status == 304) { status_ = "catalog up to date"; return false; }
    if (r.status == 404 || r.status == 403) { status_ = "catalog unavailable (is the repository public?)"; return false; }
    if (r.status == 0) { status_ = "catalog unreachable: " + (r.error.empty() ? "network error" : r.error); return false; }
    if (r.status != 200 || r.body.size() > kMaxIndexBytes) { status_ = "catalog fetch failed (HTTP " + std::to_string(r.status) + ")"; return false; }
    json j = json::parse(r.body, nullptr, false);
    if (!j.is_object() || !j.contains("games") || !j["games"].is_array()) { status_ = "catalog index was not valid JSON"; return false; }
    write_file_atomic(cfg_.dir / "index.json", r.body);
    if (auto etag = r.header("etag"); !etag.empty()) write_file_atomic(cfg_.dir / "index.etag", etag);
    index_.clear();
    for (auto& g : j["games"]) {
        if (!g.is_object() || !g.contains("id") || !g.contains("file") || !g.contains("sha256")) continue;
        CatalogEntry e;
        e.id = g.value("id", "");
        e.name = g.value("name", e.id);
        e.file = g.value("file", "");
        e.sha256 = lower(g.value("sha256", ""));
        e.min_app = g.value("min_app", "");
        e.match = g.value("match", json::object());
        e.cheats = g.value("cheats", 0);
        index_.push_back(std::move(e));
    }
    status_ = "catalog: " + std::to_string(index_.size()) + " games listed";
    return true;
}

std::optional<std::string> Catalog::download(const CatalogEntry& e, bool updated) {
    HttpOptions opt;
    opt.max_bytes = kMaxDefinitionBytes;
    HttpResponse r = cfg_.fetch(url(e.file), opt);
    if (r.status != 200) return "! could not download " + e.file + " (HTTP " + std::to_string(r.status) + ")";
    if (r.body.size() > kMaxDefinitionBytes) return "! " + e.file + " is too large";
    if (sha256_normalized(r.body) != e.sha256) return "! " + e.file + " failed its SHA-256 check and was discarded";
    auto g = parse_game_text(r.body);
    if (!g.ok()) return "! " + e.file + " is not a valid definition: " + g.message();
    if (g->id != e.id) return "! " + e.file + " does not match its index entry";
    if (!write_file_atomic(games_dir() / widen(e.file), r.body)) return "! could not save " + e.file;
    return std::string(updated ? "Updated" : "Added") + " support for " + g->name;
}

std::vector<std::string> Catalog::sync(const std::vector<LibraryGame>& detected) {
    std::vector<std::string> messages;
    if (cfg_.repo.empty()) { status_ = "catalog disabled (no repository configured)"; return messages; }
    fetch_index();
    static const std::regex file_re("^[a-z0-9][a-z0-9_-]{0,63}\\.json$");
    for (auto& e : index_) {
        if (!std::regex_match(e.file, file_re) || !find_game(detected, e.match)) continue;
        auto local = sha256_normalized_file(games_dir() / widen(e.file));
        auto bundled = cfg_.bundled_hash ? cfg_.bundled_hash(e.file) : std::nullopt;
        if (e.sha256 == local.value_or("") || e.sha256 == bundled.value_or("")) continue;         // already have exactly this content
        if (!e.min_app.empty() && compare_versions(e.min_app, cfg_.app_version) > 0) {
            messages.push_back(e.name + " needs a newer Hexwright (v" + e.min_app + "+)");
            continue;
        }
        if (auto m = download(e, local.has_value() || bundled.has_value())) messages.push_back(*m);
    }
    return messages;
}

std::vector<UiCatalogEntry> Catalog::entries_for_ui(const std::set<std::string>& loaded, const std::vector<LibraryGame>& detected) const {
    std::vector<UiCatalogEntry> out;
    for (auto& e : index_) {
        if (loaded.count(e.id)) continue;
        out.push_back({e.id, e.name, e.cheats, find_game(detected, e.match) != nullptr});
    }
    return out;
}

}  // namespace hx
