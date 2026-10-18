#include <doctest/doctest.h>

#include <string>

#include "core/definition.h"
#include "core/util.h"

using namespace hx;
using nlohmann::json;

namespace {
fs::path ck3_path() { return fs::path(HX_SOURCE_DIR) / "wandlike" / "games" / "ck3.json"; }

json good() {
    return json::parse(R"({
      "id": "demo", "name": "Demo", "exe": "demo.exe", "window_title": "Demo", "launch_args": "", "console": {},
      "match": {"steam_appid": 1}, "version": 1,
      "cheats": [
        {"id": "gold", "label": "Gold", "kind": "number", "info": "x", "set": "setgold {value}", "add": "addgold {value}"},
        {"id": "god", "label": "God", "kind": "toggle", "info": "x", "on": ["god"], "off": ["god"]}
      ]})");
}
Diagnostics check(const json& j) { return validate_game_text(j.dump()); }
bool any_has(const std::vector<std::string>& v, const std::string& s) {
    for (auto& x : v) if (x.find(s) != std::string::npos) return true;
    return false;
}
}  // namespace

TEST_CASE("the bundled Crusader Kings III definition loads") {
    auto g = load_game_file(ck3_path());
    REQUIRE(g.ok());
    CHECK(g->id == "ck3");
    CHECK(g->exe == "ck3.exe");
    CHECK(g->steam_appid == 1158310);
    CHECK(g->cheats.size() == 21);
    CHECK(g->cheats.front().id == "enable_console");
    CHECK(g->console.requires_symbol == "console");
    CHECK(g->console.toggle_scancode == 41);
    REQUIRE(g->find_symbol("paused"));
    REQUIRE(g->find_symbol("console"));
    auto* paused = g->find_symbol("paused");
    CHECK(paused->type == ValueType::U8);
    CHECK(paused->section == ".text");
    REQUIRE(paused->rip.has_value());
    CHECK(paused->rip->disp_at == 3);
    CHECK(paused->rip->insn_len == 7);
    CHECK(paused->deref == std::vector<int64_t>{32});
    auto* freeze = g->find_cheat("freeze_time");
    REQUIRE(freeze);
    CHECK(freeze->kind == CheatKind::Toggle);
    REQUIRE(freeze->memory.has_value());
    CHECK(freeze->memory->symbol == "paused");
    CHECK(freeze->memory->lock == doctest::Approx(0.02));
    CHECK(freeze->verified == Verified::Tested);
    auto* console = g->find_cheat("enable_console");
    CHECK(console->memory->restore);
    CHECK(g->find_cheat("money")->verified == Verified::Tested);
    CHECK(g->find_cheat("treasury")->requires_ == "All Under Heaven DLC");
    CHECK(g->match_spec()["steam_appid"] == 1158310);
}

TEST_CASE("bundled definition validates without errors") {
    auto text = read_file(ck3_path());
    REQUIRE(text.has_value());
    auto d = validate_game_text(*text);
    for (auto& e : d.errors) MESSAGE(e);
    CHECK(d.ok());
}

TEST_CASE("console templates") {
    CHECK(format_template("setgold {value}", "5") == "setgold 5");
    CHECK(format_template("effect add_gold = {{ value = {value} subtract = gold }}", "5000") == "effect add_gold = { value = 5000 subtract = gold }");
    CHECK_FALSE(format_template("oops {val}", "1").has_value());
    CHECK_FALSE(format_template("stray { brace", "1").has_value());
    CHECK_FALSE(format_template("stray } brace", "1").has_value());
    CHECK(format_number(5000) == "5000");
    CHECK(format_number(-3) == "-3");
    CHECK(format_number(1.5) == "1.5");
    CHECK(format_number(0.1) == "0.1");
}

TEST_CASE("console lines match the verified CK3 commands") {
    auto g = load_game_file(ck3_path());
    REQUIRE(g.ok());
    auto lines = [&](const char* id, CheatAction a, double v = 0, const char* mode = "set") { return console_lines(*g->find_cheat(id), a, v, mode); };
    CHECK(lines("dread", CheatAction::Number, 100) == std::vector<std::string>{"set_dread 100"});
    CHECK(lines("money", CheatAction::Number, 5000, "add") == std::vector<std::string>{"gold 5000"});
    CHECK(lines("money", CheatAction::Number, 5000) == std::vector<std::string>{"effect add_gold = { value = 5000 subtract = gold }"});
    CHECK(lines("age", CheatAction::Number, 30) == std::vector<std::string>{"effect set_age = 30"});
    CHECK(lines("influence", CheatAction::Number, 50, "add") == std::vector<std::string>{"add_influence 50"});
    CHECK(lines("treasury", CheatAction::Number, 50, "add") == std::vector<std::string>{"change_treasury 50"});
    CHECK(lines("instabuild", CheatAction::On) == std::vector<std::string>{"instabuild"});
    CHECK(lines("max_county_control", CheatAction::On) == std::vector<std::string>{"effect every_sub_realm_county = { change_county_control = 100 }"});
    // a number cheat with only `set` ignores the add mode and vice versa
    auto only_set = *parse_game(json::parse(R"({"id":"a","name":"A","exe":"a.exe","window_title":"A","cheats":[
        {"id":"x","label":"X","kind":"number","set":"set {value}"}]})"));
    CHECK(console_lines(only_set.cheats[0], CheatAction::Number, 3, "add") == std::vector<std::string>{"set 3"});
}

TEST_CASE("a valid minimal definition has no problems") {
    auto d = check(good());
    CHECK(d.errors.empty());
    CHECK(d.warnings.empty());
}

TEST_CASE("validator: typos get suggestions") {
    auto j = good();
    j["cheats"][0]["sett"] = "x";
    auto d = check(j);
    CHECK(any_has(d.errors, "unknown key \"sett\" (did you mean \"set\"?)"));
    j = good();
    j["verson"] = 2;
    CHECK(any_has(check(j).errors, "did you mean \"version\""));
}

TEST_CASE("validator: template mistakes") {
    for (const char* tpl : {"setgold 5", "set {val}", "effect x = { value = {value} }"}) {
        auto j = good();
        j["cheats"][0]["set"] = tpl;
        CHECK_FALSE(check(j).ok());
    }
    auto j = good();
    j["cheats"][1]["on"] = json::array({"effect x = {{ y = 1 }}"});
    CHECK(any_has(check(j).warnings, "doubled braces"));
}

TEST_CASE("validator: structure problems") {
    CHECK(any_has(validate_game_text("{").errors, "not valid JSON"));
    CHECK(any_has(validate_game_text("[]").errors, "must contain a JSON object"));
    auto j = good();
    j.erase("exe");
    CHECK(any_has(check(j).errors, "missing required key \"exe\""));
    j = good();
    j["id"] = "Bad Id";
    CHECK(any_has(check(j).errors, "must be lowercase"));
    j = good();
    j["cheats"][1]["id"] = "gold";
    CHECK(any_has(check(j).errors, "duplicate id"));
    j = good();
    j["cheats"][0].erase("set");
    j["cheats"][0].erase("add");
    CHECK(any_has(check(j).errors, "needs \"set\" and/or \"add\""));
    j = good();
    j["cheats"][1].erase("on");
    CHECK(any_has(check(j).errors, "toggle needs"));
    j = good();
    j["cheats"][0]["min"] = 0; j["cheats"][0]["max"] = 10; j["cheats"][0]["default"] = 50;
    CHECK(any_has(check(j).errors, "outside min/max"));
    j = good();
    j["cheats"][0]["kind"] = "slider";
    CHECK(any_has(check(j).errors, "kind must be one of"));
    j = good();
    j["cheats"][0]["verified"] = "maybe";
    CHECK(any_has(check(j).errors, "verified must be one of"));
    j = good();
    j["min_app"] = "soon";
    CHECK(any_has(check(j).errors, "min_app"));
    j = good();
    j["match"] = json{{"shell", "calc"}};
    CHECK(any_has(check(j).errors, "unknown match key"));
    j = good();
    j["cheats"] = json::array();
    CHECK(any_has(check(j).errors, "non-empty list"));
    CHECK(any_has(validate_game_text(std::string(300 * 1024, ' ')).errors, "larger than 256 KB"));
}

TEST_CASE("validator: hook symbols and memory cheats") {
    auto j = good();
    j["hook"] = json{{"symbols", json{{"paused", json{{"type", "uint8"}, {"aob", "48 8B 0D ?? ?? ?? ?? 41 B8"}, {"rip", json{{"disp_at", 3}, {"insn_len", 7}}}}}}}};
    j["cheats"][1] = json{{"id", "freeze"}, {"label", "Freeze"}, {"kind", "toggle"}, {"info", "x"},
                          {"memory", json{{"symbol", "paused"}, {"on", 1}, {"off", 0}, {"lock", 0.05}}}};
    auto ok = parse_game(j);
    REQUIRE(ok.ok());
    CHECK(ok->cheats[1].memory->lock == doctest::Approx(0.05));

    auto bad_symbol = j;
    bad_symbol["cheats"][1]["memory"]["symbol"] = "nope";
    CHECK(any_has(check(bad_symbol).errors, "unknown hook symbol"));
    auto no_on = j;
    no_on["cheats"][1]["memory"].erase("on");
    CHECK(any_has(check(no_on).errors, "memory toggle needs"));
    auto bad_aob = j;
    bad_aob["hook"]["symbols"]["paused"]["aob"] = "48 ZZ";
    CHECK(any_has(check(bad_aob).errors, "aob"));
    auto no_locator = j;
    no_locator["hook"]["symbols"]["paused"].erase("aob");
    no_locator["hook"]["symbols"]["paused"].erase("rip");
    CHECK(any_has(check(no_locator).errors, "needs an \"aob\""));
    auto bad_rip = j;
    bad_rip["hook"]["symbols"]["paused"]["rip"] = json{{"disp_at", 3}};
    CHECK(any_has(check(bad_rip).errors, "rip needs exactly"));
    auto bad_req = j;
    bad_req["console"] = json{{"requires_symbol", "ghost"}};
    CHECK(any_has(check(bad_req).errors, "requires_symbol"));
    auto unknown_key = j;
    unknown_key["hook"]["symbols"]["paused"]["evil"] = 1;
    CHECK(any_has(check(unknown_key).errors, "unknown key \"evil\""));
    auto legacy = good();
    legacy["cheats"][0] = json{{"id", "hp"}, {"label", "HP"}, {"kind", "number"}, {"info", "x"},
                               {"memory", json{{"module", "game.exe"}, {"offsets", json::array({"0x1A2B30", "0x10", "0x58"})}, {"type", "float"}}}};
    auto lg = parse_game(legacy);
    REQUIRE(lg.ok());
    CHECK(lg->cheats[0].memory->chain == "game.exe+1A2B30,10,58");
    CHECK(lg->cheats[0].memory->type == ValueType::F32);
}

TEST_CASE("parse_game rejects what the validator rejects") {
    auto j = good();
    j["cheats"][0]["bogus"] = true;
    CHECK_FALSE(parse_game(j).ok());
    CHECK_FALSE(parse_game_text("not json").ok());
    CHECK(parse_game(good()).ok());
}

TEST_CASE("preview shows exact commands") {
    auto g = load_game_file(ck3_path());
    REQUIRE(g.ok());
    auto lines = preview_game(*g);
    std::string all;
    for (auto& l : lines) all += l + "\n";
    CHECK(all.find("effect add_gold = { value = 1000 subtract = gold }") != std::string::npos);
    CHECK(all.find("memory symbol paused") != std::string::npos);
    CHECK(all.find("lock=0.02s") != std::string::npos);
    CHECK(all.find("Crusader Kings III") != std::string::npos);
}
