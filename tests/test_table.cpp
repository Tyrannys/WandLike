#include <doctest/doctest.h>
#include <windows.h>

#include <chrono>
#include <thread>

#include "core/table.h"
#include "core/util.h"

using namespace hx;

namespace {
struct Arena {
    uint8_t* mem = (uint8_t*)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    std::shared_ptr<Process> proc = std::make_shared<Process>(std::move(*Process::open(GetCurrentProcessId())));
    ~Arena() { VirtualFree(mem, 0, MEM_RELEASE); }
    [[nodiscard]] uint64_t addr(size_t off) const { return (uint64_t)(uintptr_t)mem + off; }
    TableEntry entry(const char* label, ValueType t, size_t off) const {
        TableEntry e;
        e.label = label;
        e.type = t;
        e.address.offsets = {(int64_t)addr(off)};
        e.freeze_value = Value::from_i64(t, 0);
        return e;
    }
};
}  // namespace

TEST_CASE("address list: read and edit typed values") {
    Arena a;
    AddressTable t;
    auto hp = t.add(a.entry("health", ValueType::F32, 0));
    auto gold = t.add(a.entry("gold", ValueType::I32, 16));
    *(float*)a.mem = 87.5f;
    *(int32_t*)(a.mem + 16) = 1234;
    CHECK(t.read(*a.proc, hp)->as_double() == doctest::Approx(87.5));
    CHECK(t.read(*a.proc, gold)->as_i64() == 1234);
    CHECK(t.write(*a.proc, gold, Value::from_i64(ValueType::I32, 99999)));
    CHECK(*(int32_t*)(a.mem + 16) == 99999);
    CHECK(t.write(*a.proc, hp, Value::from_double(ValueType::F32, 100.0)));
    CHECK(*(float*)a.mem == 100.0f);
    CHECK(t.write(*a.proc, gold, Value::from_double(ValueType::F64, 5.0)));       // value is converted to the entry's type
    CHECK(*(int32_t*)(a.mem + 16) == 5);
    std::string err;
    CHECK_FALSE(t.read(*a.proc, 99, &err).has_value());
    CHECK_FALSE(err.empty());
}

TEST_CASE("address list: pointer chain entries follow the pointer") {
    Arena a;
    AddressTable t;
    *(uint64_t*)(a.mem + 0) = a.addr(256);                  // [base] -> object
    *(int32_t*)(a.mem + 256 + 0x10) = 321;                  // object+0x10 -> value
    TableEntry e = a.entry("deep", ValueType::I32, 0);
    e.address.offsets = {(int64_t)a.addr(0), 0x10};
    auto i = t.add(e);
    CHECK(t.read(*a.proc, i)->as_i64() == 321);
    *(uint64_t*)(a.mem + 0) = a.addr(512);                  // the game reallocated the object
    *(int32_t*)(a.mem + 512 + 0x10) = 654;
    CHECK(t.read(*a.proc, i)->as_i64() == 654);             // still found: the chain is re-resolved
    *(uint64_t*)(a.mem + 0) = 0;
    std::string err;
    CHECK_FALSE(t.read(*a.proc, i, &err).has_value());
    CHECK(err.find("null") != std::string::npos);
}

TEST_CASE("freeze keeps a value pinned until unfrozen") {
    Arena a;
    AddressTable t;
    auto i = t.add(a.entry("ammo", ValueType::I32, 8));
    t.set_frozen(i, true, Value::from_i64(ValueType::I32, 999));
    FreezeEngine eng;
    eng.start(a.proc, &t, 10);
    CHECK(eng.running());
    *(volatile int32_t*)(a.mem + 8) = 3;                    // the game "spends" ammo
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(*(volatile int32_t*)(a.mem + 8) == 999);
    t.set_frozen(i, false);
    *(volatile int32_t*)(a.mem + 8) = 5;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK(*(volatile int32_t*)(a.mem + 8) == 5);            // no longer pinned
    eng.stop();
    CHECK_FALSE(eng.running());
}

TEST_CASE("apply_frozen writes every frozen entry once and reports the count") {
    Arena a;
    AddressTable t;
    auto x = t.add(a.entry("x", ValueType::I32, 0));
    auto y = t.add(a.entry("y", ValueType::U8, 4));
    t.add(a.entry("not frozen", ValueType::I32, 8));
    t.set_frozen(x, true, Value::from_i64(ValueType::I32, 11));
    t.set_frozen(y, true, Value::from_i64(ValueType::U8, 22));
    CHECK(t.apply_frozen(*a.proc) == 2);
    CHECK(*(int32_t*)a.mem == 11);
    CHECK(a.mem[4] == 22);
    CHECK(*(int32_t*)(a.mem + 8) == 0);
}

TEST_CASE("rename, retype and remove") {
    Arena a;
    AddressTable t;
    auto i = t.add(a.entry("a", ValueType::I32, 0));
    t.rename(i, "renamed");
    CHECK(t.snapshot()[0].label == "renamed");
    t.set_frozen(i, true, Value::from_i64(ValueType::I32, 7));
    t.set_type(i, ValueType::F32);
    CHECK(t.snapshot()[0].type == ValueType::F32);
    CHECK(t.snapshot()[0].freeze_value.as_double() == doctest::Approx(7.0));
    t.remove(i);
    CHECK(t.size() == 0);
    t.remove(5);                                             // out of range is ignored
}

TEST_CASE("tables save and load per game") {
    Arena a;
    AddressTable t;
    TableEntry e = a.entry("health", ValueType::F32, 0);
    e.address = *PointerChain::parse("game.exe+1A2B30,10,58");
    e.freeze_value = Value::from_double(ValueType::F32, 100.0);
    e.frozen = true;
    e.note = "player hp";
    t.add(e);
    t.add(a.entry("gold", ValueType::I64, 8));

    fs::path p = fs::temp_directory_path() / "hx-table-test.json";
    REQUIRE(t.save(p, "game.exe", "abc123"));
    AddressTable loaded;
    std::string warning;
    CHECK(loaded.load(p, "abc123", &warning).empty());
    CHECK(warning.empty());
    auto s = loaded.snapshot();
    REQUIRE(s.size() == 2);
    CHECK(s[0].label == "health");
    CHECK(s[0].type == ValueType::F32);
    CHECK(s[0].address.format() == "game.exe+1A2B30,10,58");
    CHECK(s[0].freeze_value.as_double() == doctest::Approx(100.0));
    CHECK_FALSE(s[0].frozen);                                // freezing never auto-starts
    CHECK(s[0].note == "player hp");
    CHECK(s[1].type == ValueType::I64);

    AddressTable other_build;
    CHECK(other_build.load(p, "different-hash", &warning).empty());
    CHECK_FALSE(warning.empty());                            // saved for another build
    fs::remove(p);
}

TEST_CASE("loading garbage fails with a message and keeps the old table") {
    AddressTable t;
    TableEntry e;
    e.label = "keep me";
    e.address = *PointerChain::parse("1000");
    t.add(e);
    CHECK_FALSE(t.load_json("not json", "").empty());
    CHECK_FALSE(t.load_json(R"({"entries": "nope"})", "").empty());
    CHECK_FALSE(t.load_json(R"({"entries": [{"label":"x","type":"quad","address":"10"}]})", "").empty());
    CHECK_FALSE(t.load_json(R"({"entries": [{"label":"x","type":"int32","address":"game.exe+zz"}]})", "").empty());
    CHECK(t.snapshot().size() == 1);
    CHECK(t.snapshot()[0].label == "keep me");
    CHECK_FALSE(t.load(fs::temp_directory_path() / "does-not-exist-hx.json", "").empty());
}

TEST_CASE("default table path is per exe and filesystem safe") {
    auto p = AddressTable::default_path("My Game: 2.exe");
    CHECK(p.filename().string() == "my_game__2.exe.json");
    CHECK(p.parent_path().filename() == "tables");
}
