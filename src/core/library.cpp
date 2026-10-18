#include "core/library.h"

#include <windows.h>

#include <algorithm>
#include <regex>

namespace hx {

using nlohmann::json;

namespace {

int64_t mtime_of(const fs::path& p) {
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    return ec ? -1 : (int64_t)t.time_since_epoch().count();
}

uint64_t size_of(const fs::path& p) {
    std::error_code ec;
    auto s = fs::file_size(p, ec);
    return ec ? 0 : (uint64_t)s;
}

std::string reg_string(HKEY key, const wchar_t* name) {
    wchar_t buf[1024];
    DWORD type = 0, n = sizeof buf;
    if (RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)buf, &n) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};
    return narrow(std::wstring_view(buf, wcsnlen(buf, n / sizeof(wchar_t))));
}

std::optional<fs::path> registry_steam_root() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &k) != ERROR_SUCCESS) return std::nullopt;
    std::string s = reg_string(k, L"SteamPath");
    RegCloseKey(k);
    if (s.empty()) return std::nullopt;
    return fs::path(widen(s));
}

std::string key_of(const fs::path& p) { return lower(narrow(p.wstring())); }

}  // namespace

Library::Library(fs::path cache_file, Sources src) : cache_file_(std::move(cache_file)), src_(std::move(src)) { load_cache(); }

Library Library::with_defaults() {
    Sources s;
    s.steam_root = registry_steam_root;
    const char* pd = std::getenv("PROGRAMDATA");
    s.epic_manifests = fs::path(pd ? pd : "C:\\ProgramData") / "Epic" / "EpicGamesLauncher" / "Data" / "Manifests";
    return Library(data_dir() / "library_cache.json", std::move(s));
}

void Library::load_cache() {
    auto text = read_file(cache_file_, 64u << 20);
    if (!text) return;
    json j = json::parse(*text, nullptr, false);
    if (!j.is_object() || j.value("v", 0) != 1) return;
    scanned_at_ = j.value("scanned_at", (int64_t)0);
    if (j.contains("files") && j["files"].is_object())
        for (auto it = j["files"].begin(); it != j["files"].end(); ++it)
            files_[it.key()] = {it->value("mtime", (int64_t)0), it->value("size", (uint64_t)0), it->contains("data") ? (*it)["data"] : json()};
    if (j.contains("dirs") && j["dirs"].is_object())
        for (auto it = j["dirs"].begin(); it != j["dirs"].end(); ++it) {
            DirEntry d;
            d.mtime = it->value("mtime", (int64_t)0);
            if (it->contains("names") && (*it)["names"].is_array())
                for (auto& n : (*it)["names"]) if (n.is_string()) d.names.push_back(n.get<std::string>());
            dirs_[it.key()] = std::move(d);
        }
}

void Library::save_cache() {
    json j;
    j["v"] = 1;
    j["scanned_at"] = scanned_at_;
    j["files"] = json::object();
    for (auto& [k, f] : files_) j["files"][k] = {{"mtime", f.mtime}, {"size", f.size}, {"data", f.data}};
    j["dirs"] = json::object();
    for (auto& [k, d] : dirs_) j["dirs"][k] = {{"mtime", d.mtime}, {"names", d.names}};
    std::string text = j.dump();
    if (auto old = read_file(cache_file_, 64u << 20); old && *old == text) return;
    write_file_atomic(cache_file_, text);
}

std::optional<json> Library::parsed(const fs::path& p, const std::function<json(const std::string&)>& parse) {
    std::string key = key_of(p);
    seen_files_[key] = true;
    int64_t mt = mtime_of(p);
    if (mt < 0) return std::nullopt;
    uint64_t sz = size_of(p);
    auto it = files_.find(key);
    if (it != files_.end() && it->second.mtime == mt && it->second.size == sz) { ++stats_.files_cached; return it->second.data; }
    auto text = read_file(p, 4u << 20);
    if (!text) return std::nullopt;
    ++stats_.files_parsed;
    json data = parse(*text);
    files_[key] = {mt, sz, data};
    return data;
}

std::vector<std::string> Library::listing(const fs::path& dir, const std::string& prefix, const std::string& suffix) {
    std::string key = key_of(dir) + "|" + prefix + "*" + suffix;
    seen_dirs_[key] = true;
    int64_t mt = mtime_of(dir);
    if (mt < 0) return {};
    auto it = dirs_.find(key);
    if (it != dirs_.end() && it->second.mtime == mt) { ++stats_.dirs_cached; return it->second.names; }
    std::vector<std::string> names;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::string n = narrow(e.path().filename().wstring());
        if (n.size() >= prefix.size() + suffix.size() && n.compare(0, prefix.size(), prefix) == 0 &&
            n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0)
            names.push_back(n);
    }
    std::sort(names.begin(), names.end());
    ++stats_.dirs_listed;
    dirs_[key] = {mt, names};
    return names;
}

std::vector<LibraryGame> Library::scan(bool force) {
    if (force) { files_.clear(); dirs_.clear(); }
    stats_ = {};
    seen_files_.clear();
    seen_dirs_.clear();
    std::vector<LibraryGame> all = scan_steam();
    for (auto& g : scan_epic()) all.push_back(std::move(g));
    if (src_.scan_gog) for (auto& g : scan_gog()) all.push_back(std::move(g));
    // drop cache entries nothing referenced this time
    for (auto it = files_.begin(); it != files_.end();) it = seen_files_.count(it->first) ? std::next(it) : files_.erase(it);
    for (auto it = dirs_.begin(); it != dirs_.end();) it = seen_dirs_.count(it->first) ? std::next(it) : dirs_.erase(it);
    std::sort(all.begin(), all.end(), [](const LibraryGame& a, const LibraryGame& b) {
        return a.store != b.store ? a.store < b.store : lower(a.name) < lower(b.name);
    });
    games_ = all;
    scanned_at_ = unix_seconds();
    save_cache();
    return games_;
}

std::vector<LibraryGame> Library::scan_steam() {
    std::vector<LibraryGame> out;
    if (!src_.steam_root) return out;
    auto root = src_.steam_root();
    if (!root) return out;

    std::vector<fs::path> libs{*root / "steamapps"};
    auto vdf = parsed(*root / "steamapps" / "libraryfolders.vdf", [](const std::string& text) {
        json paths = json::array();
        static const std::regex re("\"path\"\\s+\"([^\"]+)\"");
        for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) {
            std::string p = (*it)[1];
            std::string un;
            for (size_t i = 0; i < p.size(); ++i) { un.push_back(p[i]); if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') ++i; }
            paths.push_back(un);
        }
        return json{{"paths", paths}};
    });
    if (vdf && vdf->contains("paths"))
        for (auto& p : (*vdf)["paths"]) libs.push_back(fs::path(widen(p.get<std::string>())) / "steamapps");

    std::map<std::string, bool> done;
    for (auto& lib : libs) {
        std::error_code ec;
        if (!fs::exists(lib, ec) || done[key_of(lib)]) continue;
        done[key_of(lib)] = true;
        for (auto& name : listing(lib, "appmanifest_", ".acf")) {
            auto d = parsed(lib / widen(name), [](const std::string& text) {
                auto field = [&](const char* k) {
                    std::smatch m;
                    std::regex re(std::string("\"") + k + "\"\\s+\"([^\"]*)\"");
                    return std::regex_search(text, m, re) ? m[1].str() : std::string();
                };
                std::string appid = field("appid"), installdir = field("installdir");
                if (appid.empty() || installdir.empty()) return json();
                return json{{"appid", appid}, {"name", field("name")}, {"installdir", installdir}};
            });
            if (!d || d->is_null()) continue;
            fs::path folder = lib / "common" / widen((*d)["installdir"].get<std::string>());
            if (!fs::is_directory(folder, ec)) continue;
            std::string nm = (*d)["name"].get<std::string>();
            out.push_back({"steam", (*d)["appid"].get<std::string>(), nm.empty() ? (*d)["installdir"].get<std::string>() : nm, folder, ""});
        }
    }
    return out;
}

std::vector<LibraryGame> Library::scan_epic() {
    std::vector<LibraryGame> out;
    if (src_.epic_manifests.empty()) return out;
    for (auto& name : listing(src_.epic_manifests, "", ".item")) {
        auto d = parsed(src_.epic_manifests / widen(name), [](const std::string& text) {
            json j = json::parse(text, nullptr, false);
            if (!j.is_object() || !j.contains("AppName") || !j.contains("InstallLocation")) return json();
            return json{{"app", j.value("AppName", "")}, {"name", j.value("DisplayName", j.value("AppName", ""))},
                        {"loc", j.value("InstallLocation", "")}, {"exe", j.value("LaunchExecutable", "")}};
        });
        if (!d || d->is_null()) continue;
        std::error_code ec;
        fs::path loc = widen((*d)["loc"].get<std::string>());
        if (!fs::is_directory(loc, ec)) continue;
        out.push_back({"epic", (*d)["app"].get<std::string>(), (*d)["name"].get<std::string>(), loc, (*d)["exe"].get<std::string>()});
    }
    return out;
}

std::vector<LibraryGame> Library::scan_gog() {
    std::vector<LibraryGame> out;
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", 0, KEY_READ, &root) != ERROR_SUCCESS) return out;
    wchar_t sub[256];
    for (DWORD i = 0;; ++i) {
        DWORD n = (DWORD)std::size(sub);
        if (RegEnumKeyExW(root, i, sub, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        HKEY k;
        if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
        std::string id = reg_string(k, L"gameID"), name = reg_string(k, L"gameName"), path = reg_string(k, L"path"), exe = reg_string(k, L"exe");
        RegCloseKey(k);
        std::error_code ec;
        if (!id.empty() && !path.empty() && fs::is_directory(widen(path), ec))
            out.push_back({"gog", id, name.empty() ? id : name, widen(path), exe});
    }
    RegCloseKey(root);
    return out;
}

const LibraryGame* find_game(const std::vector<LibraryGame>& games, const json& match) {
    if (!match.is_object()) return nullptr;
    for (auto& g : games) {
        if (g.store == "steam" && match.contains("steam_appid") && g.id == (match["steam_appid"].is_string() ? match["steam_appid"].get<std::string>() : std::to_string(match["steam_appid"].get<int64_t>()))) return &g;
        if (g.store == "epic" && match.contains("epic") && match["epic"].is_string() && g.id == match["epic"].get<std::string>()) return &g;
        if (g.store == "gog" && match.contains("gog") &&
            g.id == (match["gog"].is_string() ? match["gog"].get<std::string>() : std::to_string(match["gog"].get<int64_t>()))) return &g;
    }
    return nullptr;
}

fs::path steam_art_path(const std::string& appid, const std::string& kind) {
    auto root = registry_steam_root();
    if (!root) return {};
    fs::path base = *root / "appcache" / "librarycache" / widen(appid);
    std::error_code ec;
    auto direct = [&](const char* file) { fs::path p = base / file; return fs::exists(p, ec) ? p : fs::path(); };
    if (kind == "cover") return direct("library_600x900.jpg");
    if (kind == "hero") return direct("library_hero.jpg");
    if (kind == "logo") return direct("logo.png");
    if (kind == "header") {
        if (auto p = direct("header.jpg"); !p.empty()) return p;
        for (auto& e : fs::directory_iterator(base, ec))
            if (e.is_directory(ec)) {
                fs::path p = e.path() / "library_header.jpg";
                if (fs::exists(p, ec)) return p;
            }
    }
    return {};
}

}  // namespace hx
