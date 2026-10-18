#include <doctest/doctest.h>
#include <windows.h>

#include <chrono>
#include <thread>

#include "core/hotkeys.h"
#include "core/session.h"
#include "core/trainer.h"

using namespace hx;
using nlohmann::json;

namespace {

// Variables the "game" exposes. They live in this exe's .data so symbols can point at them by module+rva.
volatile uint8_t g_pause = 0;
volatile uint8_t g_console = 0;
volatile uint8_t g_pause2 = 0;
float g_health = 50.0f;
int32_t g_gold = 0;

// Signatures searched in .rdata.
extern const char kSigA[] = "TRAINER_SYMBOL_SIGNATURE_9B2E4D71_A";
extern const char kSigB1[] = "TRAINER_DUPLICATE_SIGNATURE_55AA33_X";
extern const char kSigB2[] = "TRAINER_DUPLICATE_SIGNATURE_55AA33_Y";

std::string module_name() {
    auto p = Process::open(GetCurrentProcessId());
    return p->main_module()->name;
}
uint64_t rva_of(const void* p) {
    auto proc = Process::open(GetCurrentProcessId());
    return (uint64_t)(uintptr_t)p - proc->main_module()->base;
}
std::string chain_for(const volatile void* p) { return module_name() + "+" + format_hex(rva_of((const void*)p)); }

json base_def() {
    json j = {{"id", "selftest"}, {"name", "Self Test"}, {"exe", module_name()}, {"window_title", "none"}, {"launch_args", ""},
              {"console", json{{"requires_symbol", "console"}}}, {"match", json{{"steam_appid", 1}}}};
    j["hook"] = json{{"symbols", json{{"paused", json{{"type", "uint8"}, {"chain", chain_for(&g_pause)}}},
                                       {"console", json{{"type", "uint8"}, {"chain", chain_for(&g_console)}}},
                                       {"health", json{{"type", "float"}, {"chain", chain_for(&g_health)}}},
                                       {"gold", json{{"type", "int32"}, {"chain", chain_for(&g_gold)}}}}}};
    j["cheats"] = json::array({
        json{{"id", "enable_console"}, {"label", "Enable Console"}, {"kind", "toggle"}, {"info", "x"},
             {"memory", json{{"symbol", "console"}, {"on", 1}, {"off", 0}, {"lock", 0.02}, {"restore", true}}}},
        json{{"id", "freeze"}, {"label", "Freeze"}, {"kind", "toggle"}, {"info", "x"},
             {"memory", json{{"symbol", "paused"}, {"on", 1}, {"off", 0}, {"lock", 0.02}}}},
        json{{"id", "health"}, {"label", "Health"}, {"kind", "number"}, {"info", "x"}, {"min", 0}, {"max", 1000}, {"default", 100},
             {"memory", json{{"symbol", "health"}}}},
        json{{"id", "gold_mem"}, {"label", "Gold"}, {"kind", "number"}, {"info", "x"}, {"memory", json{{"symbol", "gold"}}}},
        json{{"id", "gold"}, {"label", "Console Gold"}, {"kind", "number"}, {"info", "x"}, {"set", "setgold {value}"}, {"add", "addgold {value}"},
             {"min", 0}, {"max", 100000}},
        json{{"id", "god"}, {"label", "God"}, {"kind", "toggle"}, {"info", "x"}, {"on", json::array({"god on"})}, {"off", json::array({"god off"})}},
        json{{"id", "tick"}, {"label", "Tick"}, {"kind", "toggle"}, {"info", "x"}, {"on", json::array({"tick"})}, {"interval", 0.05}},
        json{{"id", "run"}, {"label", "Run"}, {"kind", "button"}, {"info", "x"}, {"run", json::array({"do thing"})}},
    });
    return j;
}

struct Rig {
    Session session;
    std::unique_ptr<Trainer> trainer;
    explicit Rig(json def = base_def()) {
        REQUIRE(session.attach_unchecked(GetCurrentProcessId()).ok());
        auto g = parse_game(def);
        REQUIRE(g.ok());
        Trainer::Options o;
        o.dry_run = true;
        trainer = std::make_unique<Trainer>(*g, o);
        g_pause = 0; g_console = 0; g_pause2 = 0; g_health = 50.0f; g_gold = 0;
    }
    Result<Trainer::HookReport> hook() { return trainer->hook(session.process(), session.info()->exe_sha256); }
};

bool wait_for(const std::function<bool()>& cond, int ms = 1500) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return cond();
}

bool logged(const std::string& needle) {
    for (auto& l : recent_log(300))
        if (l.text.find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST_CASE("every cheat is refused until the game is hooked") {
    Rig r;
    for (auto id : {"enable_console", "freeze", "god"}) {
        auto o = r.trainer->set_toggle(id, true);
        CHECK_FALSE(o.ok);
        CHECK(o.error == "hook the game first (press Hook)");
    }
    CHECK_FALSE(r.trainer->set_number("health", 10).ok);
    CHECK_FALSE(r.trainer->set_number("gold", 10).ok);
    CHECK_FALSE(r.trainer->press("run").ok);
    CHECK_FALSE(r.trainer->hooked());
    CHECK(g_pause == 0);
}

TEST_CASE("memory toggles work as soon as the game is hooked, with a lock") {
    Rig r;
    auto rep = r.hook();
    REQUIRE(rep.ok());
    CHECK(rep->unresolved.empty());
    auto o = r.trainer->set_toggle("freeze", true);
    REQUIRE(o.ok);
    CHECK(o.on);
    CHECK(g_pause == 1);
    g_pause = 0;                                              // the game "unpauses"; the lock must pin it back
    CHECK(wait_for([] { return g_pause == 1; }));
    CHECK(r.trainer->toggle_on("freeze"));
    CHECK(r.trainer->set_toggle("freeze", false).ok);
    CHECK(g_pause == 0);
    g_pause = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(g_pause == 0);                                      // lock stopped with the toggle
}

TEST_CASE("console cheats need the console as well as the hook") {
    Rig r;
    REQUIRE(r.hook().ok());
    auto denied = r.trainer->set_number("gold", 500);
    CHECK_FALSE(denied.ok);
    CHECK(denied.error == "turn on Enable Console first");
    CHECK_FALSE(r.trainer->set_toggle("god", true).ok);
    CHECK_FALSE(r.trainer->press("run").ok);
    CHECK_FALSE(r.trainer->console_ready().first);
    CHECK_FALSE(logged("> setgold 500"));                      // nothing was typed

    REQUIRE(r.trainer->set_toggle("enable_console", true).ok);
    CHECK(g_console == 1);
    CHECK(r.trainer->console_ready().first);
    auto ok = r.trainer->set_number("gold", 500);
    REQUIRE(ok.ok);
    CHECK(ok.value == 500);
    CHECK(wait_for([] { return logged("> setgold 500"); }));
    CHECK(r.trainer->set_number("gold", 7, "add").ok);
    CHECK(wait_for([] { return logged("> addgold 7"); }));
    CHECK(r.trainer->set_toggle("god", true).ok);
    CHECK(wait_for([] { return logged("> god on"); }));
    CHECK(r.trainer->press("run").ok);
    CHECK(wait_for([] { return logged("> do thing"); }));
}

TEST_CASE("clamping keeps numbers inside min/max") {
    Rig r;
    REQUIRE(r.hook().ok());
    REQUIRE(r.trainer->set_toggle("enable_console", true).ok);
    CHECK(r.trainer->set_number("gold", 1e9).value == 100000);
    CHECK(r.trainer->set_number("gold", -5).value == 0);
    CHECK(r.trainer->set_number("health", 5000).value == 1000);
    CHECK(g_health == 1000.0f);
}

TEST_CASE("memory number cheats: set, add, float and int types") {
    Rig r;
    REQUIRE(r.hook().ok());
    CHECK(r.trainer->set_number("health", 250).ok);
    CHECK(g_health == doctest::Approx(250.0f));
    auto add = r.trainer->set_number("health", 25, "add");
    CHECK(add.ok);
    CHECK(g_health == doctest::Approx(275.0f));
    CHECK(r.trainer->set_number("gold_mem", 1234).ok);
    CHECK(g_gold == 1234);
    CHECK(r.trainer->set_number("gold_mem", -34, "add").ok);
    CHECK(g_gold == 1200);
    CHECK(r.trainer->read_memory(*r.trainer->def().find_cheat("gold_mem")->memory).value() == 1200.0);
}

TEST_CASE("restore puts the game's original value back on off and on unhook") {
    Rig r;
    REQUIRE(r.hook().ok());
    g_console = 7;
    REQUIRE(r.trainer->set_toggle("enable_console", true).ok);
    CHECK(g_console == 1);
    CHECK(r.trainer->set_toggle("enable_console", false).ok);
    CHECK(g_console == 7);

    REQUIRE(r.trainer->set_toggle("enable_console", true).ok);
    REQUIRE(r.trainer->set_toggle("freeze", true).ok);
    REQUIRE(r.trainer->set_toggle("god", true).ok);
    r.trainer->unhook();
    CHECK(g_console == 7);                                    // restored
    CHECK(g_pause == 0);                                      // off value
    CHECK_FALSE(r.trainer->hooked());
    CHECK(r.trainer->toggles().empty());
    CHECK_FALSE(r.trainer->set_toggle("freeze", true).ok);
    CHECK(r.trainer->set_toggle("freeze", false).ok);         // turning off while unhooked is harmless
}

TEST_CASE("interval toggles repeat their console command while enabled") {
    Rig r;
    REQUIRE(r.hook().ok());
    REQUIRE(r.trainer->set_toggle("enable_console", true).ok);
    size_t before = 0;
    for (auto& l : recent_log(500)) if (l.text.find("> tick") != std::string::npos) ++before;
    REQUIRE(r.trainer->set_toggle("tick", true).ok);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    size_t after = 0;
    for (auto& l : recent_log(500)) if (l.text.find("> tick") != std::string::npos) ++after;
    CHECK(after - before >= 3);
    r.trainer->set_toggle("tick", false);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    size_t stopped = 0;
    for (auto& l : recent_log(500)) if (l.text.find("> tick") != std::string::npos) ++stopped;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    size_t later = 0;
    for (auto& l : recent_log(500)) if (l.text.find("> tick") != std::string::npos) ++later;
    CHECK(later == stopped);
}

TEST_CASE("symbols found by signature: unique, ambiguous, index") {
    json def = base_def();
    def["hook"]["symbols"]["sig"] = json{{"type", "uint8"}, {"section", ".rdata"},
        {"aob", "54 52 41 49 4E 45 52 5F 53 59 4D 42 4F 4C 5F 53 49 47 4E 41 54 55 52 45 5F 39 42 32 45 34 44 37 31"}};
    def["hook"]["symbols"]["dup"] = json{{"type", "uint8"}, {"section", ".rdata"},
        {"aob", "54 52 41 49 4E 45 52 5F 44 55 50 4C 49 43 41 54 45 5F 53 49 47 4E 41 54 55 52 45 5F 35 35 41 41 33 33 5F"}};
    def["hook"]["symbols"]["dup_pick"] = json{{"type", "uint8"}, {"section", ".rdata"}, {"index", 1},
        {"aob", "54 52 41 49 4E 45 52 5F 44 55 50 4C 49 43 41 54 45 5F 53 49 47 4E 41 54 55 52 45 5F 35 35 41 41 33 33 5F"}};
    def["hook"]["symbols"]["missing"] = json{{"type", "uint8"}, {"section", ".rdata"}, {"aob", "DE AD BE EF 01 02 03 04 05 06 07 08 AA BB CC"}};
    Rig r(def);
    auto rep = r.hook();
    REQUIRE(rep.ok());
    REQUIRE(rep->resolved.count("sig"));
    CHECK(rep->resolved["sig"] == (uint64_t)(uintptr_t)kSigA);
    CHECK(rep->unresolved.count("dup"));
    CHECK(rep->unresolved["dup"].find("ambiguous") != std::string::npos);
    REQUIRE(rep->resolved.count("dup_pick"));
    CHECK(rep->resolved["dup_pick"] == (uint64_t)(uintptr_t)kSigB2);
    CHECK(rep->unresolved.count("missing"));
    CHECK(rep->unresolved["missing"].find("isn't supported yet") != std::string::npos);
    (void)kSigB1;
}

TEST_CASE("an unresolved symbol only disables the cheats that need it") {
    json def = base_def();
    def["hook"]["symbols"]["gone"] = json{{"type", "uint8"}, {"section", ".rdata"}, {"aob", "DE AD BE EF 01 02 03 04 05 06 07 08 AA BB CC"}};
    def["cheats"].push_back(json{{"id", "needs_gone"}, {"label", "Needs Gone"}, {"kind", "toggle"}, {"info", "x"},
                                 {"memory", json{{"symbol", "gone"}, {"on", 1}, {"off", 0}}}});
    Rig r(def);
    REQUIRE(r.hook().ok());
    auto bad = r.trainer->set_toggle("needs_gone", true);
    CHECK_FALSE(bad.ok);
    CHECK(bad.error.find("isn't supported yet") != std::string::npos);
    CHECK(r.trainer->set_toggle("freeze", true).ok);          // others still work
}

TEST_CASE("fixed addresses are only trusted for the right build") {
    auto proc = Process::open(GetCurrentProcessId());
    auto main = proc->main_module();
    uint64_t rva = rva_of((const void*)&g_pause2);
    auto make = [&](json sym) {
        json def = base_def();
        def["hook"]["symbols"]["fixed"] = std::move(sym);
        return def;
    };
    {   // exe size matches -> trusted
        Rig r(make(json{{"type", "uint8"}, {"rva", rva}, {"exe_size", main->size}}));
        auto rep = r.hook();
        REQUIRE(rep.ok());
        CHECK(rep->resolved.count("fixed"));
        CHECK(rep->resolved["fixed"] == main->base + rva);
    }
    {   // wrong size -> refused with an explanation
        Rig r(make(json{{"type", "uint8"}, {"rva", rva}, {"exe_size", main->size + 4096}}));
        auto rep = r.hook();
        REQUIRE(rep.ok());
        CHECK(rep->unresolved.count("fixed"));
        CHECK(rep->unresolved["fixed"].find("different game build") != std::string::npos);
    }
    {   // wrong size but the exe hash is listed -> trusted
        json def = make(json{{"type", "uint8"}, {"rva", rva}, {"exe_size", main->size + 4096}});
        Rig probe(def);
        std::string hash = probe.session.info()->exe_sha256;
        REQUIRE_FALSE(hash.empty());
        def["exe_sha256"] = json::array({hash});
        Rig r(def);
        auto rep = r.hook();
        REQUIRE(rep.ok());
        CHECK(rep->resolved.count("fixed"));
        CHECK(rep->warning.empty());
    }
}

TEST_CASE("an unknown exe hash produces a version warning") {
    json def = base_def();
    def["exe_sha256"] = json::array({std::string(64, 'a')});
    Rig r(def);
    auto rep = r.hook();
    REQUIRE(rep.ok());
    CHECK_FALSE(rep->warning.empty());
}

TEST_CASE("the trainer notices when the game exits") {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmd = std::wstring(sys) + L"\\ping.exe -n 3 127.0.0.1";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    REQUIRE(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    Sleep(200);
    Session s;
    REQUIRE(s.attach_unchecked(pi.dwProcessId).ok());
    json def = {{"id", "pinger"}, {"name", "Pinger"}, {"exe", "ping.exe"}, {"window_title", "none"},
                {"cheats", json::array({json{{"id", "god"}, {"label", "God"}, {"kind", "toggle"}, {"info", "x"}, {"on", json::array({"god"})}}})}};
    auto g = parse_game(def);
    REQUIRE(g.ok());
    Trainer t(*g, Trainer::Options{true});
    std::atomic<bool> exited{false};
    t.set_on_exit([&] { exited = true; });
    REQUIRE(t.hook(s.process()).ok());
    CHECK(t.hooked());
    TerminateProcess(pi.hProcess, 0);
    CHECK(wait_for([&] { return !t.hooked(); }, 3000));
    CHECK(wait_for([&] { return exited.load(); }, 1000));
    CHECK_FALSE(s.alive());                                    // the session detaches itself too
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

TEST_CASE("session: safety check refuses before anything is opened") {
    Session s;
    auto r = s.attach(0xFFFFFFF0u);
    CHECK_FALSE(r.ok());
    CHECK_FALSE(s.attached());
}

TEST_CASE("hotkeys: parsing and dispatch") {
    auto c = parse_combo("Ctrl+Shift+F5");
    REQUIRE(c.has_value());
    CHECK(c->mods == (MOD_CONTROL | MOD_SHIFT));
    CHECK(c->vk == VK_F5);
    CHECK(format_combo(*c) == "Ctrl+Shift+F5");
    CHECK(parse_combo("Alt+A")->vk == 'A');
    CHECK(parse_combo("Num5")->vk == VK_NUMPAD5);
    CHECK(parse_combo("Ctrl+`")->vk == VK_OEM_3);
    CHECK_FALSE(parse_combo("Ctrl+Shift").has_value());
    CHECK_FALSE(parse_combo("Ctrl+Banana").has_value());
    CHECK_FALSE(parse_combo("").has_value());
    CHECK_FALSE(parse_combo("A+B").has_value());

    HotkeyManager hk;
    std::atomic<int> fired{0};
    std::string err;
    auto id = hk.add("Ctrl+Alt+Shift+F24", [&] { ++fired; }, &err);
    REQUIRE(id.has_value());
    hk.simulate(*id);
    CHECK(fired == 1);
    CHECK_FALSE(hk.add("Ctrl+Nope", [] {}, &err).has_value());
    CHECK(err == "unrecognised key combination");
    // the same combination cannot be registered twice
    HotkeyManager other;
    CHECK_FALSE(other.add("Ctrl+Alt+Shift+F24", [] {}, &err).has_value());
    CHECK(err == "already used by another program");
    hk.clear();
    CHECK(other.add("Ctrl+Alt+Shift+F24", [] {}, &err).has_value());   // free again after clear()
}
