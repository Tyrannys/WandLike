#include <doctest/doctest.h>
#include <windows.h>

#include <cstring>
#include <vector>

#include "core/hash.h"
#include "core/process.h"
#include "core/safety.h"
#include "core/util.h"

using namespace hx;

TEST_CASE("string helpers") {
    CHECK(lower("CK3.EXE") == "ck3.exe");
    CHECK(trim("  a b \t") == "a b");
    CHECK(iequals("Ck3.Exe", "ck3.exe"));
    CHECK(split("a,b,,c", ',').size() == 4);
    CHECK(narrow(widen("Crusader Kings \xC3\xA9")) == "Crusader Kings \xC3\xA9");
    CHECK(format_hex(0x5C7E378) == "5C7E378");
    CHECK(format_hex(0xAB, 4) == "00AB");
    CHECK(parse_hex("0x7FF6FCAA0000") == 0x7FF6FCAA0000ull);
    CHECK(parse_hex("1f") == 0x1F);
    CHECK_FALSE(parse_hex("xyz").has_value());
    CHECK_FALSE(parse_hex("").has_value());
}

TEST_CASE("sha256 known vectors") {
    CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_normalized("a\r\nb\r\n") == sha256_hex("a\nb\n"));
    CHECK(sha256_normalized("a\nb\n") == sha256_hex("a\nb\n"));
}

TEST_CASE("file round trip is atomic and readable") {
    fs::path p = fs::temp_directory_path() / "hx-test-file.txt";
    REQUIRE(write_file_atomic(p, "hello"));
    CHECK(read_file(p).value() == "hello");
    REQUIRE(write_file_atomic(p, "replaced"));
    CHECK(read_file(p).value() == "replaced");
    CHECK(sha256_file(p).value() == sha256_hex("replaced"));
    fs::remove(p);
    CHECK_FALSE(read_file(p).has_value());
}

TEST_CASE("process listing finds this test process") {
    auto procs = list_processes();
    bool found = false;
    for (auto& p : procs)
        if (p.pid == GetCurrentProcessId()) found = true;
    CHECK(found);
    CHECK_FALSE(find_pids(narrow(exe_path().filename().wstring())).empty());
}

TEST_CASE("attach to a process, read, write, detach") {
    static volatile int32_t value = 1234;          // lives in this exe's data section
    static uint8_t buffer[64];
    auto r = Process::open(GetCurrentProcessId());
    REQUIRE(r.ok());
    Process& p = *r;
    CHECK(p.alive());
    CHECK(p.is64() == (sizeof(void*) == 8));
    CHECK(p.pointer_size() == sizeof(void*));
    CHECK_FALSE(p.name().empty());

    auto addr = (uint64_t)(uintptr_t)&value;
    CHECK(p.read_value<int32_t>(addr).value() == 1234);
    CHECK(p.write_value<int32_t>(addr, 99));
    CHECK(value == 99);

    std::memset(buffer, 0xAB, sizeof buffer);
    uint8_t out[8] = {};
    CHECK(p.read((uint64_t)(uintptr_t)buffer + 4, out, 8));
    CHECK(out[0] == 0xAB);
    CHECK_FALSE(p.read(0x10, out, 8));                       // unmapped
    CHECK(p.read_partial(0x10, out, 8) == 0);

    void* self_ptr = (void*)(uintptr_t)&value;
    static void* holder;
    holder = self_ptr;
    CHECK(p.read_pointer((uint64_t)(uintptr_t)&holder).value() == (uint64_t)(uintptr_t)self_ptr);
}

TEST_CASE("writing to read-only pages lifts and restores protection") {
    void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READONLY);
    REQUIRE(page);
    auto r = Process::open(GetCurrentProcessId());
    REQUIRE(r.ok());
    uint32_t v = 0xCAFEBABE;
    CHECK(r->write((uint64_t)(uintptr_t)page, &v, 4));
    CHECK(*(uint32_t*)page == 0xCAFEBABE);
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(page, &mbi, sizeof mbi);
    CHECK(mbi.Protect == PAGE_READONLY);                      // protection was put back
    VirtualFree(page, 0, MEM_RELEASE);
}

TEST_CASE("regions honour the filter") {
    void* rw = VirtualAlloc(nullptr, 1 << 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    void* ro = VirtualAlloc(nullptr, 1 << 16, MEM_COMMIT | MEM_RESERVE, PAGE_READONLY);
    auto r = Process::open(GetCurrentProcessId());
    REQUIRE(r.ok());
    auto has = [&](const std::vector<Region>& v, void* a) {
        for (auto& x : v)
            if ((uint64_t)(uintptr_t)a >= x.base && (uint64_t)(uintptr_t)a < x.base + x.size) return true;
        return false;
    };
    RegionFilter writable;                                   // default: writable private/image memory
    auto w = r->regions(writable);
    CHECK(has(w, rw));
    CHECK_FALSE(has(w, ro));
    RegionFilter all;
    all.writable_only = false;
    auto a = r->regions(all);
    CHECK(has(a, ro));
    RegionFilter window;
    window.lo = (uint64_t)(uintptr_t)rw;
    window.hi = window.lo + 4096;
    auto clipped = r->regions(window);
    REQUIRE_FALSE(clipped.empty());
    CHECK(clipped.front().base == window.lo);
    CHECK(clipped.front().size <= 4096);
    VirtualFree(rw, 0, MEM_RELEASE);
    VirtualFree(ro, 0, MEM_RELEASE);
}

TEST_CASE("modules and PE sections of this exe") {
    auto r = Process::open(GetCurrentProcessId());
    REQUIRE(r.ok());
    auto main = r->main_module();
    REQUIRE(main.has_value());
    CHECK(main->base == (uint64_t)(uintptr_t)GetModuleHandleW(nullptr));
    auto secs = r->sections(main->base);
    REQUIRE(secs.ok());
    bool text = false, data = false;
    for (auto& s : *secs) {
        if (s.name == ".text") { text = true; CHECK(s.executable); }
        if (s.name == ".data") { data = true; CHECK(s.writable); }
    }
    CHECK(text);
    CHECK(data);
    CHECK(r->module("kernel32.dll").has_value());
    CHECK_FALSE(r->module("definitely-not-loaded.dll").has_value());
}

TEST_CASE("a process that does not exist reports a clear error") {
    auto r = Process::open(0xFFFFFFF0u);
    CHECK_FALSE(r.ok());
    CHECK_FALSE(r.message().empty());
}

TEST_CASE("exited process is reported as not alive") {
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"cmd.exe /c exit 0";
    REQUIRE(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    auto r = Process::open(pi.dwProcessId);
    WaitForSingleObject(pi.hProcess, 5000);
    if (r.ok()) CHECK_FALSE(r->alive());                      // already gone is also fine (open would fail)
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

// --------------------------------------------------------------------- safety
TEST_CASE("safety: ordinary single-player games are allowed") {
    auto v = evaluate_safety("ck3.exe", {"ck3.exe", "kernel32.dll", "PDXSDK.dll"}, {"steam.exe", "discord.exe", "ck3.exe"}, {"Spooler", "Dhcp"});
    CHECK(v.allowed);
    CHECK(v.reason.empty());
}

TEST_CASE("safety: multiplayer / competitive games are refused") {
    for (auto exe : {"VALORANT-Win64-Shipping.exe", "cs2.exe", "FortniteClient-Win64-Shipping.exe", "r5apex.exe", "TslGame.exe"}) {
        auto v = evaluate_safety(exe, {}, {}, {});
        CHECK_FALSE(v.allowed);
        CHECK(v.reason.find("multiplayer") != std::string::npos);
    }
}

TEST_CASE("safety: anti-cheat modules in the target are refused") {
    auto v = evaluate_safety("somegame.exe", {"somegame.exe", "EasyAntiCheat_x64.dll"}, {}, {});
    CHECK_FALSE(v.allowed);
    CHECK(v.matches.front() == "easyanticheat_x64.dll");
    CHECK_FALSE(evaluate_safety("somegame.exe", {"BEClient_x64.dll"}, {}, {}).allowed);
}

TEST_CASE("safety: a running anti-cheat process or driver blocks attaching to anything") {
    auto eac = evaluate_safety("ck3.exe", {}, {"EasyAntiCheat_EOS.exe"}, {});
    CHECK_FALSE(eac.allowed);
    CHECK(eac.reason.find("anti-cheat") != std::string::npos);
    CHECK_FALSE(evaluate_safety("ck3.exe", {}, {"BEService.exe"}, {}).allowed);
    CHECK_FALSE(evaluate_safety("ck3.exe", {}, {}, {"vgk"}).allowed);              // Vanguard kernel driver
    CHECK_FALSE(evaluate_safety("ck3.exe", {}, {}, {"BEDaisy"}).allowed);
    CHECK_FALSE(evaluate_safety("ck3.exe", {}, {"FACEIT.exe"}, {}).allowed);
    CHECK_FALSE(evaluate_safety("ck3.exe", {}, {"PnkBstrA.exe"}, {}).allowed);
    CHECK_FALSE(evaluate_safety("ck3.exe", {}, {}, {"Randgrid"}).allowed);         // Ricochet
}

TEST_CASE("safety: the blocklists are non-trivial and lower-case") {
    for (auto* list : {&blocked_game_exes(), &anticheat_process_names(), &anticheat_module_names(), &anticheat_service_names()}) {
        CHECK(list->size() > 10);
        for (auto& s : *list) CHECK(s == lower(s));
    }
}

TEST_CASE("safety: live check on this machine returns a verdict without crashing") {
    auto v = check_attach(GetCurrentProcessId(), "hextests.exe");
    CHECK((v.allowed || !v.reason.empty()));
}
