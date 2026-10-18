#include <doctest/doctest.h>
#include <windows.h>

#include <cstring>
#include <vector>

#include "core/aob.h"
#include "core/pointer.h"
#include "core/process.h"

using namespace hx;

namespace {
// Lives in .rdata of the test exe, so scan_module(".rdata") can find it again.
extern const char kNeedle[] = "HEXWRIGHT_AOB_NEEDLE_7F3A91C2_0123456789";
// Lives in .data: a pointer variable the chain test follows from the module base.
uint64_t g_ptr_holder = 0;

std::vector<uint8_t> bytes(std::initializer_list<int> v) {
    std::vector<uint8_t> out;
    for (int x : v) out.push_back((uint8_t)x);
    return out;
}
Pattern pat(const char* s) { return *Pattern::parse(s); }
}  // namespace

TEST_CASE("pattern parsing") {
    auto p = Pattern::parse("48 8B 0D ?? ?? ?? ?? 41 b8");
    REQUIRE(p.ok());
    CHECK(p->size() == 9);
    CHECK(p->mask[3] == 0x00);
    CHECK(p->mask[0] == 0xFF);
    CHECK(p->to_string() == "48 8B 0D ?? ?? ?? ?? 41 B8");
    auto nib = Pattern::parse("4? ?B");
    REQUIRE(nib.ok());
    CHECK(nib->mask[0] == 0xF0);
    CHECK(nib->mask[1] == 0x0F);
    CHECK(Pattern::parse("?").ok() == false);                  // only wildcards
    CHECK_FALSE(Pattern::parse("").ok());
    CHECK_FALSE(Pattern::parse("48 8").ok());
    CHECK_FALSE(Pattern::parse("48 ZZ").ok());
    CHECK_FALSE(Pattern::parse("123").ok());
}

TEST_CASE("find_in_buffer: exact, wildcards, nibbles, overlap, limit") {
    auto buf = bytes({0x00, 0x48, 0x8B, 0x0D, 0x11, 0x22, 0x33, 0x44, 0x41, 0xB8, 0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x41, 0xB8, 0x90});
    auto a = find_in_buffer(buf.data(), buf.size(), pat("48 8B 0D ?? ?? ?? ?? 41 B8"));
    REQUIRE(a.size() == 2);
    CHECK(a[0] == 1);
    CHECK(a[1] == 10);
    CHECK(find_in_buffer(buf.data(), buf.size(), pat("48 8B 0D ?? ?? ?? ?? 41 B8"), 1).size() == 1);   // limit
    CHECK(find_in_buffer(buf.data(), buf.size(), pat("AA BB")).empty());
    CHECK(find_in_buffer(buf.data(), buf.size(), pat("4? 8B")).size() == 2);                          // nibble wildcard
    CHECK(find_in_buffer(buf.data(), buf.size(), pat("?? 8B 0D 11")).size() == 1);                    // leading wildcard
    CHECK(find_in_buffer(buf.data(), buf.size(), pat("B8 90")).front() == 18);                         // match at the very end
    auto rep = bytes({0xAA, 0xAA, 0xAA, 0xAA});
    CHECK(find_in_buffer(rep.data(), rep.size(), pat("AA AA")).size() == 3);                           // overlapping matches
    CHECK(find_in_buffer(rep.data(), 1, pat("AA AA")).empty());                                        // buffer shorter than pattern
}

TEST_CASE("scan_ranges across chunk edges") {
    size_t size = 10u << 20;                                   // larger than the 4 MB chunk
    auto* mem = (uint8_t*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(mem);
    auto proc = Process::open(GetCurrentProcessId());
    uint8_t needle[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    size_t at_edge = (4u << 20) - 3;                           // straddles the first chunk boundary
    std::memcpy(mem + at_edge, needle, sizeof needle);
    std::memcpy(mem + (9u << 20), needle, sizeof needle);
    auto r = scan_ranges(*proc, {{(uint64_t)(uintptr_t)mem, size}}, pat("DE AD BE EF ?? ?? 03 04"));
    REQUIRE(r.ok());
    REQUIRE(r->size() == 2);
    CHECK((*r)[0] == (uint64_t)(uintptr_t)mem + at_edge);
    CHECK((*r)[1] == (uint64_t)(uintptr_t)mem + (9u << 20));
    VirtualFree(mem, 0, MEM_RELEASE);
}

TEST_CASE("scan_module finds data in a named section of this exe") {
    auto proc = Process::open(GetCurrentProcessId());
    auto mod = proc->main_module();
    REQUIRE(mod.has_value());
    auto r = scan_module(*proc, *mod, ".rdata", pat("48 45 58 57 52 49 47 48 54 5F 41 4F 42 5F 4E 45 45 44 4C 45 5F ?? ?? ?? ?? ?? ?? ?? ?? 5F 30 31"));
    REQUIRE(r.ok());
    REQUIRE_FALSE(r->empty());
    CHECK((*r)[0] == (uint64_t)(uintptr_t)kNeedle);
    CHECK_FALSE(scan_module(*proc, *mod, ".nosuchsection", pat("48 45")).ok());
    auto whole = scan_module(*proc, *mod, "", pat("48 45 58 57 52 49 47 48 54 5F 41 4F 42 5F 4E 45 45 44 4C 45"));
    REQUIRE(whole.ok());
    CHECK_FALSE(whole->empty());
}

TEST_CASE("RIP-relative resolution") {
    auto* mem = (uint8_t*)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    auto proc = Process::open(GetCurrentProcessId());
    // 48 8B 0D <disp32>   mov rcx, [rip+disp32]   (7 bytes)
    size_t insn = 100, target = 900;
    int32_t disp = (int32_t)(target - (insn + 7));
    mem[insn] = 0x48; mem[insn + 1] = 0x8B; mem[insn + 2] = 0x0D;
    std::memcpy(mem + insn + 3, &disp, 4);
    auto addr = resolve_rip(*proc, (uint64_t)(uintptr_t)mem + insn, 3, 7);
    REQUIRE(addr.has_value());
    CHECK(*addr == (uint64_t)(uintptr_t)mem + target);
    // backwards reference (negative displacement)
    size_t insn2 = 2000, target2 = 50;
    int32_t disp2 = (int32_t)((int64_t)target2 - (int64_t)(insn2 + 8));
    std::memcpy(mem + insn2 + 4, &disp2, 4);
    CHECK(*resolve_rip(*proc, (uint64_t)(uintptr_t)mem + insn2, 4, 8) == (uint64_t)(uintptr_t)mem + target2);
    CHECK_FALSE(resolve_rip(*proc, 0x10, 3, 7).has_value());
    VirtualFree(mem, 0, MEM_RELEASE);
}

TEST_CASE("pointer chain parse / format") {
    auto c = PointerChain::parse("ck3.exe+5C7E378,20,8");
    REQUIRE(c.ok());
    CHECK(c->module == "ck3.exe");
    CHECK(c->offsets == std::vector<int64_t>{0x5C7E378, 0x20, 8});
    CHECK(c->format() == "ck3.exe+5C7E378,20,8");
    auto abs = PointerChain::parse("7FF6FCAA0000,10");
    REQUIRE(abs.ok());
    CHECK(abs->module.empty());
    CHECK(abs->offsets[0] == 0x7FF6FCAA0000ll);
    auto neg = PointerChain::parse("game.dll+100,-8");
    REQUIRE(neg.ok());
    CHECK(neg->offsets[1] == -8);
    CHECK(neg->format() == "game.dll+100,-8");
    auto bare = PointerChain::parse("game.dll");
    REQUIRE(bare.ok());
    CHECK(bare->module == "game.dll");
    CHECK(bare->offsets[0] == 0);
    CHECK_FALSE(PointerChain::parse("").ok());
    CHECK_FALSE(PointerChain::parse("ck3.exe+zz").ok());
    CHECK_FALSE(PointerChain::parse("ck3.exe+10,qq").ok());
}

TEST_CASE("pointer chain resolution: absolute and from a module base") {
    auto* mem = (uint8_t*)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    auto proc = Process::open(GetCurrentProcessId());
    uint64_t base = (uint64_t)(uintptr_t)mem;
    // *(base) -> base+512; the object's field at +0x20 holds a pointer to base+1024; the value sits at +8 of that
    *(uint64_t*)(mem + 0) = base + 512;
    *(uint64_t*)(mem + 512 + 0x20) = base + 1024;
    *(int32_t*)(mem + 1024 + 8) = 4242;

    PointerChain simple;
    simple.offsets = {(int64_t)base, 0x20};                // *(base) = base+512 ; +0x20 = base+544 (address of the field)
    auto r1 = resolve_chain(*proc, simple);
    REQUIRE(r1.ok());
    CHECK(*r1 == base + 512 + 0x20);

    PointerChain deep;
    deep.offsets = {(int64_t)base, 0x20, 8};               // *(base)=base+512 ; +0x20 ; *(base+544)=base+1024 ; +8
    auto r2 = resolve_chain(*proc, deep);
    REQUIRE(r2.ok());
    CHECK(*r2 == base + 1024 + 8);
    CHECK(proc->read_value<int32_t>(*r2).value() == 4242);

    // from a module base: a pointer variable in the exe's .data points into the arena
    g_ptr_holder = base + 512;
    auto main = proc->main_module();
    PointerChain modchain;
    modchain.module = main->name;
    modchain.offsets = {(int64_t)((uint64_t)(uintptr_t)&g_ptr_holder - main->base), 0x20};
    auto r3 = resolve_chain(*proc, modchain);
    REQUIRE(r3.ok());
    CHECK(*r3 == base + 512 + 0x20);

    // failure modes are errors with a message, not crashes
    PointerChain missing_module;
    missing_module.module = "nope.dll";
    missing_module.offsets = {0};
    CHECK_FALSE(resolve_chain(*proc, missing_module).ok());
    PointerChain bad_read;
    bad_read.offsets = {0x10, 0x20};
    CHECK_FALSE(resolve_chain(*proc, bad_read).ok());
    *(uint64_t*)(mem + 0) = 0;                              // null pointer mid-chain
    auto null_mid = resolve_chain(*proc, simple);
    CHECK_FALSE(null_mid.ok());
    CHECK(null_mid.message().find("null") != std::string::npos);
    VirtualFree(mem, 0, MEM_RELEASE);
}

TEST_CASE("32-bit target: open, modules, PE32 sections, 4-byte pointers") {
    wchar_t sys[MAX_PATH];
    GetSystemWow64DirectoryW(sys, MAX_PATH);                  // empty on a 32-bit OS
    std::wstring dir = sys;
    if (dir.empty()) return;
    std::wstring cmd = dir + L"\\ping.exe -n 6 127.0.0.1";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    REQUIRE(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    Sleep(300);
    auto r = Process::open(pi.dwProcessId);
    REQUIRE(r.ok());
    CHECK_FALSE(r->is64());
    CHECK(r->pointer_size() == 4);
    auto main = r->main_module();
    REQUIRE(main.has_value());
    CHECK(main->base < 0x100000000ull);
    uint16_t mz = 0;
    REQUIRE(r->read(main->base, &mz, 2));
    CHECK(mz == 0x5A4D);
    auto secs = r->sections(main->base);
    REQUIRE(secs.ok());
    bool text = false;
    for (auto& s : *secs) text |= s.name == ".text";
    CHECK(text);
    CHECK(r->module("kernel32.dll").has_value());
    // every region of a WOW64 process stays below 4 GB
    RegionFilter f;
    f.writable_only = false;
    f.include_mapped = true;
    f.include_executable = true;
    for (auto& reg : r->regions(f)) CHECK(reg.base < 0x100000000ull);
    // a 4-byte pointer read at the module base reads the MZ header (0x00905A4D) as a 32-bit value
    CHECK(r->read_pointer(main->base).value() == 0x00905A4Dull);
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}
