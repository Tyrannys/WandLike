#include <doctest/doctest.h>
#include <windows.h>

#include <fstream>

#include "core/authoring.h"
#include "core/definition.h"
#include "core/util.h"

using namespace hx;
using nlohmann::json;

namespace {
fs::path fresh(const char* tag) {
    fs::path p = fs::temp_directory_path() / (std::string("hx-auth-") + tag + "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(now_ms()));
    fs::create_directories(p);
    return p;
}
void touch(const fs::path& p, size_t bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << std::string(bytes, 'x');
}
}  // namespace

TEST_CASE("slugify") {
    CHECK(slugify("Europa: Universalis IV!") == "europa-universalis-iv");
    CHECK(slugify("  --  ") == "game");
    CHECK(slugify("Crusader Kings III") == "crusader-kings-iii");
    CHECK(slugify(std::string(100, 'a')).size() == 48);
}

TEST_CASE("guess_exe prefers the biggest real executable and skips installers") {
    fs::path dir = fresh("exe");
    touch(dir / "game.exe", 100);
    touch(dir / "unins000.exe", 9999);
    touch(dir / "UnityCrashHandler64.exe", 5000);
    touch(dir / "bin" / "main.exe", 500);
    LibraryGame g{"steam", "1", "G", dir, ""};
    CHECK(guess_exe(g) == "main.exe");
    LibraryGame epic{"epic", "x", "G", dir, "Binaries\\Win64\\Thing.exe"};
    CHECK(guess_exe(epic) == "Thing.exe");                       // recorded by the store: just the file name
    LibraryGame empty{"steam", "1", "G", fresh("empty"), ""};
    CHECK(guess_exe(empty) == "game.exe");
}

TEST_CASE("a scaffolded definition validates cleanly for every store") {
    fs::path dir = fresh("scaf");
    touch(dir / "game.exe", 100);
    for (LibraryGame g : {LibraryGame{"steam", "236850", "Europa Universalis IV", dir, ""}, LibraryGame{"epic", "Salt", "Fortnite", dir, "f.exe"},
                          LibraryGame{"gog", "123", "Resident Evil", dir, "re.exe"}}) {
        json j = scaffold_definition(g);
        auto d = validate_game_text(j.dump());
        for (auto& e : d.errors) MESSAGE(e);
        CHECK(d.ok());
        CHECK(j["id"] == slugify(g.name));
    }
    json steam = scaffold_definition({"steam", "236850", "EU4", dir, ""});
    CHECK(steam["match"]["steam_appid"] == 236850);
    CHECK(steam["steam_appid"] == 236850);
    CHECK(scaffold_definition({"epic", "Salt", "F", dir, "f.exe"})["match"]["epic"] == "Salt");
    CHECK(scaffold_definition({"gog", "123", "G", dir, "g.exe"})["match"]["gog"] == "123");
}

TEST_CASE("the committed catalog index is current") {
    fs::path games = fs::path(HX_SOURCE_DIR) / "wandlike" / "games";
    std::string err;
    std::string fresh_text = build_index_text(games, &err);
    INFO(err);
    REQUIRE_FALSE(fresh_text.empty());
    auto committed = read_file(games / "_index.json");
    REQUIRE(committed.has_value());
    std::string c;
    for (char ch : *committed) if (ch != '\r') c.push_back(ch);
    CHECK(c == fresh_text);   // run `hexctl catalog` after editing a definition
}

TEST_CASE("index building rejects bad definitions and mismatched file names") {
    fs::path dir = fresh("idx");
    std::string err;
    {
        std::ofstream(dir / "a.json") << R"({"id":"a","name":"A","exe":"a.exe","window_title":"A","cheats":[{"id":"x","label":"X","kind":"toggle","on":["x"]}]})";
        auto text = build_index_text(dir, &err);
        CHECK_FALSE(text.empty());
        auto j = json::parse(text);
        CHECK(j["games"].size() == 1);
        CHECK(j["games"][0]["file"] == "a.json");
        CHECK(j["games"][0]["cheats"] == 1);
        CHECK(j["games"][0]["sha256"].get<std::string>().size() == 64);
    }
    std::ofstream(dir / "wrongname.json") << R"({"id":"b","name":"B","exe":"b.exe","window_title":"B","cheats":[{"id":"x","label":"X","kind":"toggle","on":["x"]}]})";
    CHECK(build_index_text(dir, &err).empty());
    CHECK(err.find("must equal the id") != std::string::npos);
    fs::remove(dir / "wrongname.json");
    std::ofstream(dir / "bad.json") << "{ broken";
    CHECK(build_index_text(dir, &err).empty());
    CHECK(err.find("bad.json") != std::string::npos);
}
