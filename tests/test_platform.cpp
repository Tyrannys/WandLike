#include <doctest/doctest.h>
#include <windows.h>

#include <fstream>

#include "core/catalog.h"
#include "core/games.h"
#include "core/hash.h"
#include "core/library.h"
#include "core/settings.h"
#include "core/updater.h"

using namespace hx;
using nlohmann::json;

namespace {

fs::path fresh_dir(const char* tag) {
    fs::path p = fs::temp_directory_path() / (std::string("hx-") + tag + "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(now_ms()));
    fs::create_directories(p);
    return p;
}

void write(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

std::string acf(const std::string& id, const std::string& name, const std::string& dir) {
    return "\"AppState\"\n{\n\t\"appid\"\t\t\"" + id + "\"\n\t\"name\"\t\t\"" + name + "\"\n\t\"installdir\"\t\t\"" + dir + "\"\n}\n";
}

struct Libs {
    fs::path root = fresh_dir("lib");
    fs::path steam = root / "Steam";
    fs::path apps = steam / "steamapps";
    fs::path epic = root / "epic";
    fs::path cache = root / "cache.json";
    Libs() {
        std::string p = steam.string();
        std::string esc;
        for (char c : p) { esc.push_back(c); if (c == '\\') esc.push_back('\\'); }
        write(apps / "libraryfolders.vdf", "\"libraryfolders\" { \"0\" { \"path\" \"" + esc + "\" } }");
        add_steam("1158310", "Crusader Kings III", "Crusader Kings III");
        add_steam("10", "Counter-Strike", "Counter-Strike");
        fs::create_directories(root / "epicgames" / "Fortnite");
        std::string loc = (root / "epicgames" / "Fortnite").string();
        std::string eloc;
        for (char c : loc) { eloc.push_back(c); if (c == '\\') eloc.push_back('\\'); }
        write(epic / "Salt.item", "{\"AppName\":\"Salt\",\"DisplayName\":\"Fortnite\",\"InstallLocation\":\"" + eloc + "\",\"LaunchExecutable\":\"f.exe\"}");
    }
    void add_steam(const std::string& id, const std::string& name, const std::string& dir) {
        fs::create_directories(apps / "common" / dir);
        write(apps / ("appmanifest_" + id + ".acf"), acf(id, name, dir));
    }
    Library make() const {
        Library::Sources s;
        s.steam_root = [r = steam]() -> std::optional<fs::path> { return r; };
        s.epic_manifests = epic;
        s.scan_gog = false;
        return Library(cache, s);
    }
};

bool has_id(const std::vector<LibraryGame>& g, const std::string& store, const std::string& id) {
    for (auto& x : g) if (x.store == store && x.id == id) return true;
    return false;
}

}  // namespace

// ----------------------------------------------------------------- settings
TEST_CASE("settings: defaults, round trip, unknown keys preserved") {
    fs::path p = fresh_dir("set") / "settings.json";
    {
        Settings s(p);
        CHECK(s.app().autoupdate);
        CHECK(s.app().overlay_hotkey == "Ctrl+Shift+F10");
        AppSettings a = s.app();
        a.hotkeys_enabled = false;
        a.console_mode = "combo";
        s.set_app(a);
        s.set_last_game("ck3");
        s.set_pinned("ck3", "money", true);
        s.set_pinned("ck3", "dread", true);
        s.set_hotkey("ck3", "freeze_time", "Ctrl+Shift+F5");
        s.remember_value("ck3", "money", 5000, "add");
        s.set_option("ck3", "fast_culture", "interval", 30);
        s.set_favorite("ck3", true);
        s.set_auto_apply("ck3", true);
        s.set_last_played("ck3", 1234567);
        Preset pr;
        pr.toggles = {"enable_console", "instabuild"};
        pr.numbers["money"] = {5000, "set"};
        s.set_preset("ck3", pr);
    }
    Settings again(p);                                       // a fresh load sees everything
    CHECK_FALSE(again.app().hotkeys_enabled);
    CHECK(again.app().console_mode == "combo");
    CHECK(again.last_game() == "ck3");
    CHECK(again.pins("ck3") == std::vector<std::string>{"dread", "money"});    // sorted
    CHECK(again.hotkey("ck3", "freeze_time") == "Ctrl+Shift+F5");
    CHECK(again.value("ck3", "money", 0) == 5000);
    CHECK(again.mode("ck3", "money") == "add");
    CHECK(again.mode("ck3", "other") == "set");
    CHECK(again.option("ck3", "fast_culture", "interval", 20) == 30);
    CHECK(again.favorite("ck3"));
    CHECK(again.auto_apply("ck3"));
    CHECK(again.last_played("ck3") == 1234567);
    auto pr = again.preset("ck3");
    REQUIRE(pr.has_value());
    CHECK(pr->toggles.size() == 2);
    CHECK(pr->numbers["money"].first == 5000);
    again.set_pinned("ck3", "money", false);
    again.set_hotkey("ck3", "freeze_time", "");
    CHECK(again.pins("ck3") == std::vector<std::string>{"dread"});
    CHECK(again.hotkey("ck3", "freeze_time").empty());
    CHECK_FALSE(Settings(fresh_dir("set2") / "none.json").preset("ck3").has_value());
}

TEST_CASE("settings: reads files written by the earlier Python build") {
    fs::path p = fresh_dir("setold") / "settings.json";
    write(p, R"({"game":"ck3","app":{"autoupdate":false,"overlay_hotkey":"Ctrl+Alt+O","accent":"teal","future_key":1},
                 "g.ck3":{"pins":["money"],"hotkeys":{"freeze_time":"F9"},"values":{"money":5000.0},"modes":{"money":"add"},
                 "preset":{"toggles":["instabuild"],"numbers":{"money":[5000.0,"set"]}},"last_played":1792257559}})");
    Settings s(p);
    CHECK_FALSE(s.app().autoupdate);
    CHECK(s.app().overlay_hotkey == "Ctrl+Alt+O");
    CHECK(s.pins("ck3") == std::vector<std::string>{"money"});
    CHECK(s.hotkey("ck3", "freeze_time") == "F9");
    CHECK(s.mode("ck3", "money") == "add");
    CHECK(s.preset("ck3")->numbers["money"].second == "set");
    s.set_app(s.app());
    auto text = read_file(p);
    CHECK(text->find("future_key") != std::string::npos);    // unknown keys survive a save
}

TEST_CASE("settings: a corrupt file falls back to defaults") {
    fs::path p = fresh_dir("setbad") / "settings.json";
    write(p, "{ this is not json");
    Settings s(p);
    CHECK(s.app().autoupdate);
    CHECK(s.last_game().empty());
    s.set_last_game("x");
    CHECK(Settings(p).last_game() == "x");
}

// ------------------------------------------------------------------ library
TEST_CASE("library finds games in every store") {
    Libs l;
    auto lib = l.make();
    auto games = lib.scan();
    CHECK(has_id(games, "steam", "1158310"));
    CHECK(has_id(games, "steam", "10"));
    CHECK(has_id(games, "epic", "Salt"));
    auto* ck3 = find_game(games, json{{"steam_appid", 1158310}});
    REQUIRE(ck3);
    CHECK(ck3->name == "Crusader Kings III");
    CHECK(find_game(games, json{{"epic", "Salt"}})->exe == "f.exe");
    CHECK(find_game(games, json{{"steam_appid", 999}}) == nullptr);
    CHECK(find_game(games, json{{"steam_appid", "10"}}) != nullptr);          // string ids work too
    CHECK(find_game(games, json{{"gog", "5"}}) == nullptr);
}

TEST_CASE("library: a second scan reads nothing, and the cache survives a restart") {
    Libs l;
    auto lib = l.make();
    lib.scan();
    CHECK(lib.stats().files_parsed > 0);
    lib.scan();
    CHECK(lib.stats().files_parsed == 0);
    CHECK(lib.stats().dirs_listed == 0);
    CHECK(lib.stats().files_cached > 0);
    auto restarted = l.make();
    restarted.scan();
    CHECK(restarted.stats().files_parsed == 0);
}

TEST_CASE("library: installs, uninstalls and changed manifests are noticed") {
    Libs l;
    auto lib = l.make();
    lib.scan();
    l.add_steam("281990", "Stellaris", "Stellaris");
    auto games = lib.scan();
    CHECK(has_id(games, "steam", "281990"));
    CHECK(lib.stats().files_parsed == 1);                                      // only the new manifest was read
    fs::remove(l.apps / "appmanifest_281990.acf");
    CHECK_FALSE(has_id(lib.scan(), "steam", "281990"));
    write(l.apps / "appmanifest_10.acf", acf("10", "CS Renamed", "Counter-Strike") + "\n");
    games = lib.scan();
    CHECK(find_game(games, json{{"steam_appid", 10}})->name == "CS Renamed");
    CHECK(lib.stats().files_parsed == 1);
    lib.scan(true);
    CHECK(lib.stats().files_parsed > 1);                                       // force re-reads everything
}

TEST_CASE("library: a manifest whose install folder is gone is ignored") {
    Libs l;
    fs::remove_all(l.apps / "common" / "Counter-Strike");
    auto games = l.make().scan();
    CHECK_FALSE(has_id(games, "steam", "10"));
}

// ------------------------------------------------------------------ catalog
namespace {

std::string def_text(const std::string& id, const std::string& notes = "", const std::string& min_app = "") {
    json j = {{"id", id}, {"name", "New Game"}, {"exe", "ng.exe"}, {"window_title", "NG"}, {"version", 1}, {"match", json{{"steam_appid", 77}}},
              {"notes", notes},
              {"cheats", json::array({json{{"id", "gold"}, {"label", "Gold"}, {"kind", "number"}, {"info", "x"}, {"set", "gold {value}"}}})}};
    if (!min_app.empty()) j["min_app"] = min_app;
    return j.dump(2);
}

struct FakeRepo {
    std::map<std::string, std::string> files;
    std::string etag = "e0";
    int status = 200;
    std::vector<std::pair<std::string, std::string>> calls;       // (file, if-none-match)
    void publish(const std::string& body, const std::string& id = "newgame", const std::string& min_app = "") {
        files[id + ".json"] = body;
        json idx = json::array();
        for (auto& [name, text] : files) {
            if (name == "_index.json") continue;
            json d = json::parse(text);
            json e = {{"id", d["id"]}, {"name", d["name"]}, {"file", name}, {"sha256", sha256_normalized(text)}, {"match", d["match"]}, {"cheats", 1}};
            if (!min_app.empty() && d["id"] == id) e["min_app"] = min_app;
            idx.push_back(e);
        }
        files["_index.json"] = json{{"schema", 1}, {"games", idx}}.dump();
        etag = "e" + std::to_string(calls.size()) + "x" + std::to_string(files.size());
    }
    HttpFetch fetch() {
        return [this](const std::string& url, const HttpOptions& o) {
            HttpResponse r;
            std::string name = url.substr(url.rfind('/') + 1);
            auto inm = o.headers.find("If-None-Match");
            calls.emplace_back(name, inm == o.headers.end() ? "" : inm->second);
            if (name == "_index.json") {
                if (status != 200) { r.status = status; return r; }
                if (inm != o.headers.end() && inm->second == etag) { r.status = 304; return r; }
                r.status = 200;
                r.body = files[name];
                r.headers["etag"] = etag;
                return r;
            }
            auto it = files.find(name);
            r.status = it == files.end() ? 404 : 200;
            if (it != files.end()) r.body = it->second;
            return r;
        };
    }
    [[nodiscard]] size_t downloads() const {
        size_t n = 0;
        for (auto& c : calls) if (c.first != "_index.json") ++n;
        return n;
    }
};

std::vector<LibraryGame> detected() { return {{"steam", "77", "New Game", "x", ""}}; }

struct CatRig {
    FakeRepo repo;
    fs::path dir = fresh_dir("cat");
    std::map<std::string, std::string> bundled;
    std::unique_ptr<Catalog> cat;
    void make(const std::string& app_version = "1.0.0") {
        Catalog::Config c;
        c.repo = "me/wl";
        c.branch = "main";
        c.path = "wandlike/games";
        c.dir = dir;
        c.app_version = app_version;
        c.fetch = repo.fetch();
        c.bundled_hash = [this](const std::string& f) -> std::optional<std::string> {
            auto it = bundled.find(f);
            if (it == bundled.end()) return std::nullopt;
            return sha256_normalized(it->second);
        };
        cat = std::make_unique<Catalog>(c);
    }
    CatRig() { make(); }
};

}  // namespace

TEST_CASE("catalog: versions compare numerically") {
    CHECK(compare_versions("2.0.0", "1.10.3") > 0);
    CHECK(compare_versions("v1.2.0", "1.2.0") == 0);
    CHECK(compare_versions("1.9.9", "1.10.0") < 0);
    CHECK(compare_versions("1.0", "1.0.0") == 0);
}

TEST_CASE("catalog: only definitions for detected games are downloaded") {
    CatRig r;
    r.repo.publish(def_text("newgame"));
    CHECK(r.cat->sync({}).empty());
    CHECK(r.repo.downloads() == 0);
    auto msgs = r.cat->sync(detected());
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0] == "Added support for New Game");
    CHECK(fs::exists(r.cat->games_dir() / "newgame.json"));
    CHECK(r.cat->sync(detected()).empty());                                   // nothing more to do
}

TEST_CASE("catalog: changed content is detected without any version bump") {
    CatRig r;
    r.repo.publish(def_text("newgame"));
    r.cat->sync(detected());
    r.repo.publish(def_text("newgame", "edited on GitHub"));                  // same "version": 1
    auto msgs = r.cat->sync(detected());
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0] == "Updated support for New Game");
    CHECK(read_file(r.cat->games_dir() / "newgame.json")->find("edited on GitHub") != std::string::npos);
}

TEST_CASE("catalog: the index is fetched with its ETag") {
    CatRig r;
    r.repo.publish(def_text("newgame"));
    r.cat->sync(detected());
    r.cat->sync(detected());
    CHECK(r.repo.calls.back().first == "_index.json");
    CHECK(r.repo.calls.back().second == r.repo.etag);
    CHECK(r.cat->status().find("up to date") != std::string::npos);
}

TEST_CASE("catalog: a bundled copy that is already current is not downloaded") {
    CatRig r;
    std::string body = def_text("newgame");
    r.bundled["newgame.json"] = body;
    r.repo.publish(body);
    CHECK(r.cat->sync(detected()).empty());
    CHECK(r.repo.downloads() == 0);
    r.repo.publish(def_text("newgame", "newer than bundled"));
    auto msgs = r.cat->sync(detected());
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0] == "Updated support for New Game");
}

TEST_CASE("catalog: line endings do not cause re-downloads") {
    CatRig r;
    std::string body = def_text("newgame");
    r.repo.publish(body);
    r.cat->sync(detected());
    std::string crlf;
    for (char c : body) { if (c == '\n') crlf += "\r\n"; else crlf.push_back(c); }
    write(r.cat->games_dir() / "newgame.json", crlf);                          // a Windows-style copy of the same content
    size_t n = r.repo.downloads();
    r.cat->sync(detected());
    CHECK(r.repo.downloads() == n);
}

TEST_CASE("catalog: a new app version drops downloaded copies and re-pulls") {
    CatRig r;
    r.repo.publish(def_text("newgame"));
    r.cat->sync(detected());
    REQUIRE(fs::exists(r.cat->games_dir() / "newgame.json"));
    r.make("1.1.0");                                                           // the app was updated
    CHECK_FALSE(fs::exists(r.cat->games_dir() / "newgame.json"));
    auto msgs = r.cat->sync(detected());
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0] == "Added support for New Game");
}

TEST_CASE("catalog: tampered, invalid, mismatched and path-traversal files are rejected") {
    {
        CatRig r;
        r.repo.publish(def_text("newgame"));
        r.repo.files["newgame.json"] = def_text("newgame", "tampered");
        auto m = r.cat->sync(detected());
        REQUIRE(m.size() == 1);
        CHECK(m[0].find("SHA-256") != std::string::npos);
        CHECK_FALSE(fs::exists(r.cat->games_dir() / "newgame.json"));
    }
    {
        CatRig r;
        json bad = json::parse(def_text("newgame"));
        bad["evil_field"] = 1;
        r.repo.publish(bad.dump());
        auto m = r.cat->sync(detected());
        REQUIRE(m.size() == 1);
        CHECK(m[0].find("not a valid definition") != std::string::npos);
    }
    {
        CatRig r;
        r.repo.publish(def_text("newgame"));
        json idx = json::parse(r.repo.files["_index.json"]);
        idx["games"][0]["file"] = "../../evil.json";
        r.repo.files["_index.json"] = idx.dump();
        CHECK(r.cat->sync(detected()).empty());
        CHECK(r.repo.downloads() == 0);
    }
    {
        CatRig r;
        r.repo.publish(def_text("newgame"));
        json idx = json::parse(r.repo.files["_index.json"]);
        idx["games"][0]["id"] = "other";
        r.repo.files["_index.json"] = idx.dump();
        auto m = r.cat->sync(detected());
        REQUIRE(m.size() == 1);
        CHECK(m[0].find("does not match") != std::string::npos);
    }
}

TEST_CASE("catalog: unreachable, private and too-new definitions are handled") {
    CatRig r;
    r.repo.status = 404;
    CHECK(r.cat->sync(detected()).empty());
    CHECK(r.cat->status().find("unavailable") != std::string::npos);

    CatRig newer;
    newer.repo.publish(def_text("newgame"), "newgame", "9.0.0");
    auto m = newer.cat->sync(detected());
    REQUIRE(m.size() == 1);
    CHECK(m[0].find("needs a newer Hexwright") != std::string::npos);
    CHECK(newer.repo.downloads() == 0);
}

TEST_CASE("catalog: entries for the Explore page") {
    CatRig r;
    r.repo.publish(def_text("newgame"));
    r.cat->fetch_index();
    auto e = r.cat->entries_for_ui({}, detected());
    REQUIRE(e.size() == 1);
    CHECK(e[0].installed);
    CHECK(r.cat->entries_for_ui({"newgame"}, detected()).empty());
}

// --------------------------------------------------------------- game store
TEST_CASE("game store: bundled, downloaded and user definitions layer in that order") {
    fs::path root = fresh_dir("store");
    GameStore::Config c;
    c.catalog_dir = root / "catalog";
    c.user_dir = root / "user";
    c.app_version = "2.0.0";
    {
        GameStore s(c);
        const LoadedGame* ck3 = s.find("ck3");
        REQUIRE(ck3);
        CHECK(ck3->source == "bundled");
        CHECK(ck3->def.cheats.size() == 21);
    }
    json dl = json::parse(def_text("ck3", "from the catalog"));
    write(c.catalog_dir / "ck3.json", dl.dump());
    write(c.catalog_dir / "broken.json", "{ nope");
    write(c.catalog_dir / "toonew.json", def_text("toonew", "", "99.0.0"));
    {
        GameStore s(c);
        CHECK(s.find("ck3")->source == "catalog");
        CHECK(s.find("ck3")->def.notes == "from the catalog");
        CHECK(s.find("broken") == nullptr);                                    // bad files are skipped, not fatal
        CHECK(s.find("toonew") == nullptr);                                    // needs a newer app
    }
    write(c.user_dir / "ck3.json", def_text("ck3", "mine"));
    write(c.user_dir / "mygame.json", def_text("mygame"));
    GameStore s(c);
    CHECK(s.find("ck3")->source == "user");
    CHECK(s.find("ck3")->def.notes == "mine");
    CHECK(s.find("mygame") != nullptr);
}

// ------------------------------------------------------------------ updater
namespace {
std::string release_json(const std::string& tag, const std::string& url, const std::string& digest = "", bool pre = false, bool draft = false,
                         const std::string& asset = "Hexwright-Setup.exe") {
    json a = {{"name", asset}, {"browser_download_url", url}};
    if (!digest.empty()) a["digest"] = digest;
    return json{{"tag_name", tag}, {"prerelease", pre}, {"draft", draft}, {"body", "notes"}, {"html_url", "https://github.com/me/wl/releases/tag/" + tag},
                {"assets", json::array({a})}}.dump();
}
UpdaterConfig up(const std::string& body, int status = 200, const std::string& current = "1.0.0") {
    UpdaterConfig c = default_updater_config();
    c.repo = "me/wl";
    c.current_version = current;
    c.fetch = [=](const std::string&, const HttpOptions&) { HttpResponse r; r.status = status; r.body = body; return r; };
    return c;
}
const std::string kUrl = "https://github.com/me/wl/releases/download/v1.1.0/Hexwright-Setup.exe";
}  // namespace

TEST_CASE("updater: finds a newer published release") {
    std::string digest = "sha256:" + std::string(64, 'a');
    auto u = check_for_update(up(release_json("v1.1.0", kUrl, digest)));
    REQUIRE(u.has_value());
    CHECK(u->version == "1.1.0");
    CHECK(u->url == kUrl);
    CHECK(u->sha256 == std::string(64, 'a'));
    CHECK(u->notes == "notes");
    CHECK_FALSE(check_for_update(up(release_json("v1.0.0", kUrl))).has_value());               // same version
    CHECK_FALSE(check_for_update(up(release_json("v0.9.0", kUrl))).has_value());               // older
    CHECK_FALSE(check_for_update(up(release_json("v1.1.0", kUrl, "", true))).has_value());     // prerelease
    CHECK_FALSE(check_for_update(up(release_json("v1.1.0", kUrl, "", false, true))).has_value());   // draft
    CHECK_FALSE(check_for_update(up(release_json("v1.1.0", kUrl, "", false, false, "Other.exe"))).has_value());
}

TEST_CASE("updater: refuses URLs outside the repository and reports failures") {
    std::string err;
    CHECK_FALSE(check_for_update(up(release_json("v1.1.0", "https://evil.example/Hexwright-Setup.exe")), &err).has_value());
    CHECK(err.find("unexpected URL") != std::string::npos);
    CHECK_FALSE(check_for_update(up("", 404), &err).has_value());
    CHECK(err.find("public") != std::string::npos);
    CHECK_FALSE(check_for_update(up("not json"), &err).has_value());
    CHECK_FALSE(check_for_update(up("", 0), &err).has_value());
    UpdaterConfig none = up("");
    none.repo.clear();
    CHECK_FALSE(check_for_update(none).has_value());
}

TEST_CASE("updater: downloads are verified against the SHA-256 digest") {
    std::string data = "MZ pretend installer";
    UpdaterConfig cfg = up("");
    cfg.download = [&](const std::string&, const fs::path& dest, const ProgressFn& p, const HttpOptions&) {
        write(dest, data);
        if (p) p(data.size(), data.size());
        HttpResponse r;
        r.status = 200;
        return r;
    };
    UpdateInfo info;
    info.url = kUrl;
    info.version = "1.1.0";
    info.sha256 = sha256_hex(data);
    uint64_t seen = 0;
    auto ok = download_update(cfg, info, [&](uint64_t d, uint64_t) { seen = d; });
    REQUIRE(ok.ok());
    CHECK(read_file(*ok).value() == data);
    CHECK(seen == data.size());
    info.sha256 = std::string(64, '0');
    auto bad = download_update(cfg, info);
    CHECK_FALSE(bad.ok());
    CHECK(bad.message().find("SHA-256") != std::string::npos);
    info.sha256.clear();                                                      // no digest published: still downloads
    CHECK(download_update(cfg, info).ok());
    cfg.download = [](const std::string&, const fs::path&, const ProgressFn&, const HttpOptions&) { HttpResponse r; r.status = 0; r.error = "offline"; return r; };
    CHECK_FALSE(download_update(cfg, info).ok());
}
