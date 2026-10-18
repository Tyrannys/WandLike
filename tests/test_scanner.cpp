#include <doctest/doctest.h>
#include <windows.h>

#include <cstring>
#include <vector>

#include "core/process.h"
#include "core/scanner.h"
#include "core/value.h"

using namespace hx;

namespace {

// A private, writable block of memory in this process that tests can scan in isolation.
struct Arena {
    static constexpr size_t kSize = 1 << 20;
    uint8_t* mem;
    Process proc;
    Arena() : mem((uint8_t*)VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)), proc(std::move(*Process::open(GetCurrentProcessId()))) {
        std::memset(mem, 0, kSize);
    }
    ~Arena() { VirtualFree(mem, 0, MEM_RELEASE); }
    [[nodiscard]] uint64_t addr(size_t off = 0) const { return (uint64_t)(uintptr_t)mem + off; }
    template <class T> void put(size_t off, T v) { std::memcpy(mem + off, &v, sizeof v); }
    template <class T> T get(size_t off) const { T v; std::memcpy(&v, mem + off, sizeof v); return v; }
    [[nodiscard]] ScanConfig cfg(ValueType t) const {
        ScanConfig c;
        c.type = t;
        c.regions.lo = addr();
        c.regions.hi = addr() + kSize;
        return c;
    }
};

ScanParams exact(ValueType t, const char* text) { return {ScanOp::Exact, *Value::parse(t, text), {}}; }
ScanParams op(ScanOp o) { return {o, {}, {}}; }
bool has(const Scanner& s, uint64_t a) {
    for (auto& h : s.hits()) if (h.address == a) return true;
    return false;
}

}  // namespace

TEST_CASE("Value parse / format round trips") {
    CHECK(Value::parse(ValueType::I32, "123")->as_i64() == 123);
    CHECK(Value::parse(ValueType::I32, "-5")->as_i64() == -5);
    CHECK(Value::parse(ValueType::I32, "0x10")->as_i64() == 16);
    CHECK(Value::parse(ValueType::F32, "1.5")->as_double() == doctest::Approx(1.5));
    CHECK(Value::parse(ValueType::F64, "2.25")->format() == "2.25");
    CHECK(Value::parse(ValueType::I64, "9000000000")->as_i64() == 9000000000ll);
    CHECK_FALSE(Value::parse(ValueType::I32, "abc").has_value());
    CHECK_FALSE(Value::parse(ValueType::U8, "300").has_value());      // out of range is rejected, not wrapped
    CHECK_FALSE(Value::parse(ValueType::I32, "").has_value());
    CHECK(type_size(ValueType::I64) == 8);
    CHECK(parse_type("float") == ValueType::F32);
    CHECK(parse_type("INT32") == ValueType::I32);
    CHECK_FALSE(parse_type("quad").has_value());
    uint8_t raw[8];
    Value::of<int32_t>(ValueType::I32, -2).to_bytes(raw);
    CHECK(Value::from_bytes(ValueType::I32, raw).as_i64() == -2);
}

TEST_CASE("exact scan finds every type") {
    Arena a;
    a.put<uint8_t>(100, 77);
    a.put<int16_t>(200, 31000);
    a.put<int32_t>(300, 123456);
    a.put<int64_t>(400, 5000000000ll);
    a.put<float>(500, 3.5f);
    a.put<double>(600, 7.125);
    struct Case { ValueType t; const char* text; size_t off; };
    for (Case c : {Case{ValueType::U8, "77", 100}, Case{ValueType::I16, "31000", 200}, Case{ValueType::I32, "123456", 300},
                   Case{ValueType::I64, "5000000000", 400}, Case{ValueType::F32, "3.5", 500}, Case{ValueType::F64, "7.125", 600}}) {
        Scanner s;
        auto r = s.first_scan(a.proc, a.cfg(c.t), exact(c.t, c.text));
        REQUIRE(r.ok());
        CHECK(*r == 1);
        CHECK(has(s, a.addr(c.off)));
    }
}

TEST_CASE("narrowing with exact value across changes") {
    Arena a;
    a.put<int32_t>(0, 100);
    a.put<int32_t>(64, 100);
    a.put<int32_t>(128, 100);
    Scanner s;
    REQUIRE(*s.first_scan(a.proc, a.cfg(ValueType::I32), exact(ValueType::I32, "100")) == 3);
    a.put<int32_t>(0, 90);      // the "health" value drops
    a.put<int32_t>(64, 100);
    a.put<int32_t>(128, 95);
    REQUIRE(*s.next_scan(a.proc, exact(ValueType::I32, "90")) == 1);
    CHECK(has(s, a.addr(0)));
    CHECK(a.proc.write_value<int32_t>(s.hits()[0].address, 999));
    CHECK(a.get<int32_t>(0) == 999);
}

TEST_CASE("unknown initial value then increased / decreased / changed / unchanged") {
    Arena a;
    a.put<int32_t>(0, 10);      // will increase
    a.put<int32_t>(4, 10);      // will decrease
    a.put<int32_t>(8, 10);      // will stay
    a.put<int32_t>(12, 10);     // will change to something bigger then smaller
    Scanner s;
    auto first = s.first_scan(a.proc, a.cfg(ValueType::I32), op(ScanOp::Unknown));
    REQUIRE(first.ok());
    CHECK(s.is_snapshot());
    CHECK(*first == Arena::kSize / 4);                                 // every aligned dword

    a.put<int32_t>(0, 15);
    a.put<int32_t>(4, 5);
    a.put<int32_t>(12, 11);
    SUBCASE("increased") {
        REQUIRE(*s.next_scan(a.proc, op(ScanOp::Increased)) == 2);
        CHECK(has(s, a.addr(0)));
        CHECK(has(s, a.addr(12)));
        CHECK_FALSE(s.is_snapshot());
        a.put<int32_t>(0, 20);                                         // compare to the values of the previous scan
        REQUIRE(*s.next_scan(a.proc, op(ScanOp::Increased)) == 1);
        CHECK(has(s, a.addr(0)));
    }
    SUBCASE("decreased") {
        REQUIRE(*s.next_scan(a.proc, op(ScanOp::Decreased)) == 1);
        CHECK(has(s, a.addr(4)));
    }
    SUBCASE("changed") {
        REQUIRE(*s.next_scan(a.proc, op(ScanOp::Changed)) == 3);
    }
    SUBCASE("unchanged keeps untouched memory (zero-filled) and the 10 at offset 8") {
        auto n = *s.next_scan(a.proc, op(ScanOp::Unchanged));
        CHECK(n == Arena::kSize / 4 - 3);
        CHECK(has(s, a.addr(8)));
        CHECK_FALSE(has(s, a.addr(0)));
    }
    SUBCASE("increased by an exact amount") {
        REQUIRE(*s.next_scan(a.proc, {ScanOp::IncreasedBy, *Value::parse(ValueType::I32, "5"), {}}) == 1);
        CHECK(has(s, a.addr(0)));
    }
    SUBCASE("decreased by an exact amount") {
        REQUIRE(*s.next_scan(a.proc, {ScanOp::DecreasedBy, *Value::parse(ValueType::I32, "5"), {}}) == 1);
        CHECK(has(s, a.addr(4)));
    }
}

TEST_CASE("between, bigger than, smaller than") {
    Arena a;
    a.put<int32_t>(0, 5);
    a.put<int32_t>(4, 50);
    a.put<int32_t>(8, 500);
    Scanner s;
    ScanParams between{ScanOp::Between, *Value::parse(ValueType::I32, "10"), *Value::parse(ValueType::I32, "100")};
    REQUIRE(*s.first_scan(a.proc, a.cfg(ValueType::I32), between) == 1);
    CHECK(has(s, a.addr(4)));
    Scanner big;
    REQUIRE(*big.first_scan(a.proc, a.cfg(ValueType::I32), {ScanOp::Bigger, *Value::parse(ValueType::I32, "100"), {}}) == 1);
    CHECK(has(big, a.addr(8)));
    Scanner small;
    auto n = *small.first_scan(a.proc, a.cfg(ValueType::I32), {ScanOp::Smaller, *Value::parse(ValueType::I32, "10"), {}});
    CHECK(n > 1000);    // zero-filled memory is smaller than 10
    CHECK(has(small, a.addr(0)));
}

TEST_CASE("float rounding mode and exact mode") {
    Arena a;
    a.put<float>(0, 12.3456f);
    ScanConfig exact_cfg = a.cfg(ValueType::F32);
    Scanner s1;
    CHECK(*s1.first_scan(a.proc, exact_cfg, exact(ValueType::F32, "12.3")) == 0);          // exact: no match
    ScanConfig rounded = a.cfg(ValueType::F32);
    rounded.float_decimals = 1;                                                             // rounds to 12.3
    Scanner s2;
    CHECK(*s2.first_scan(a.proc, rounded, exact(ValueType::F32, "12.3")) == 1);
}

TEST_CASE("alignment: natural alignment skips odd offsets, alignment 1 finds them") {
    Arena a;
    a.put<int32_t>(2, 0x5EED1234);                // misaligned dword
    Scanner natural;
    CHECK(*natural.first_scan(a.proc, a.cfg(ValueType::I32), exact(ValueType::I32, "1592594996")) == 0);
    ScanConfig byte_aligned = a.cfg(ValueType::I32);
    byte_aligned.alignment = 1;
    Scanner every;
    CHECK(*every.first_scan(a.proc, byte_aligned, exact(ValueType::I32, "1592594996")) == 1);
    CHECK(has(every, a.addr(2)));
}

TEST_CASE("values straddling an 8 MB chunk boundary are still found") {
    // Arena smaller than a chunk, so test the boundary logic with a larger allocation.
    size_t size = 20u << 20;
    auto* mem = (uint8_t*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(mem);
    int32_t magic = 0x13572468;
    std::memcpy(mem + (8u << 20) - 2, &magic, 4);                 // straddles the first chunk boundary at alignment 1
    auto proc = Process::open(GetCurrentProcessId());
    ScanConfig c;
    c.type = ValueType::I32;
    c.alignment = 1;
    c.regions.lo = (uint64_t)(uintptr_t)mem;
    c.regions.hi = c.regions.lo + size;
    Scanner s;
    auto r = s.first_scan(*proc, c, exact(ValueType::I32, "324478056"));
    REQUIRE(r.ok());
    CHECK(*r == 1);
    CHECK(s.hits()[0].address == (uint64_t)(uintptr_t)mem + (8u << 20) - 2);
    VirtualFree(mem, 0, MEM_RELEASE);
}

TEST_CASE("many hits are batched and narrowed correctly") {
    Arena a;
    for (size_t i = 0; i < 5000; ++i) a.put<int32_t>(i * 64, 7777);
    Scanner s;
    REQUIRE(*s.first_scan(a.proc, a.cfg(ValueType::I32), exact(ValueType::I32, "7777")) == 5000);
    for (size_t i = 0; i < 5000; i += 2) a.put<int32_t>(i * 64, 8888);
    CHECK(*s.next_scan(a.proc, exact(ValueType::I32, "7777")) == 2500);
    CHECK(*s.next_scan(a.proc, op(ScanOp::Unchanged)) == 2500);
    CHECK(*s.next_scan(a.proc, exact(ValueType::I32, "1")) == 0);
}

TEST_CASE("errors: next scan without a first scan, mismatched types, cancel, cap") {
    Arena a;
    Scanner s;
    CHECK_FALSE(s.next_scan(a.proc, exact(ValueType::I32, "1")).ok());
    CHECK_FALSE(s.first_scan(a.proc, a.cfg(ValueType::I32), op(ScanOp::Increased)).ok());        // needs a previous scan
    CHECK_FALSE(s.first_scan(a.proc, a.cfg(ValueType::I32), exact(ValueType::F32, "1")).ok());   // type mismatch

    ScanProgress cancel;
    cancel.cancel = true;
    auto r = s.first_scan(a.proc, a.cfg(ValueType::I32), op(ScanOp::Unknown), &cancel);
    CHECK_FALSE(r.ok());
    CHECK_FALSE(s.has_scan());

    ScanConfig capped = a.cfg(ValueType::I32);
    capped.max_results = 10;
    auto cap = s.first_scan(a.proc, capped, {ScanOp::Smaller, *Value::parse(ValueType::I32, "10"), {}});
    CHECK_FALSE(cap.ok());
    CHECK(cap.message().find("Too many results") != std::string::npos);

    ScanConfig small_snapshot = a.cfg(ValueType::I32);
    small_snapshot.max_snapshot_bytes = 1024;
    CHECK_FALSE(s.first_scan(a.proc, small_snapshot, op(ScanOp::Unknown)).ok());
}

TEST_CASE("progress reports the whole region as done") {
    Arena a;
    ScanProgress prog;
    Scanner s;
    REQUIRE(s.first_scan(a.proc, a.cfg(ValueType::I32), exact(ValueType::I32, "42"), &prog).ok());
    CHECK(prog.fraction() == doctest::Approx(1.0f));
}

TEST_CASE("scanning a process that exited fails cleanly") {
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"cmd.exe /c exit 0";
    REQUIRE(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    Arena a;
    Scanner s;
    REQUIRE(*s.first_scan(a.proc, a.cfg(ValueType::I32), op(ScanOp::Unknown)) > 0);
    auto dead = Process::open(pi.dwProcessId);
    WaitForSingleObject(pi.hProcess, 5000);
    if (dead.ok()) CHECK_FALSE(s.next_scan(*dead, op(ScanOp::Changed)).ok());
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}
