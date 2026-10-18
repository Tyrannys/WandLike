#include "core/process.h"

#include <tlhelp32.h>

#include <algorithm>
#include <cstring>

namespace hx {

// ------------------------------------------------------------- enumeration
std::vector<ProcessInfo> list_processes() {
    std::vector<ProcessInfo> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof e;
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        out.push_back({e.th32ProcessID, e.th32ParentProcessID, narrow(e.szExeFile)});
    CloseHandle(snap);
    return out;
}

std::vector<uint32_t> find_pids(std::string_view exe_name) {
    std::vector<uint32_t> pids;
    for (auto& p : list_processes())
        if (iequals(p.name, exe_name)) pids.push_back(p.pid);
    return pids;
}

std::vector<ModuleInfo> list_modules(uint32_t pid) {
    std::vector<ModuleInfo> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return out;
    MODULEENTRY32W e{};
    e.dwSize = sizeof e;
    for (BOOL ok = Module32FirstW(snap, &e); ok; ok = Module32NextW(snap, &e))
        out.push_back({narrow(e.szModule), narrow(e.szExePath), (uint64_t)(uintptr_t)e.modBaseAddr, e.modBaseSize});
    CloseHandle(snap);
    return out;
}

std::optional<std::string> process_image_path(uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return std::nullopt;
    wchar_t buf[MAX_PATH * 2];
    DWORD n = (DWORD)std::size(buf);
    std::optional<std::string> r;
    if (QueryFullProcessImageNameW(h, 0, buf, &n)) r = narrow(std::wstring_view(buf, n));
    CloseHandle(h);
    return r;
}

// ----------------------------------------------------------------- Process
static constexpr DWORD kAccess = PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION | SYNCHRONIZE;

Result<Process> Process::open(uint32_t pid) {
    HANDLE h = OpenProcess(kAccess, FALSE, pid);
    if (!h) {
        DWORD code = GetLastError();
        std::string msg = code == ERROR_ACCESS_DENIED
            ? "Access denied. If the game runs as administrator, run Hexwright as administrator too."
            : code == ERROR_INVALID_PARAMETER ? "That process is no longer running."
            : "Could not open the process (Windows error " + std::to_string(code) + ").";
        return Error{msg, code};
    }
    Process p;
    p.h_ = h;
    p.pid_ = pid;
    for (auto& info : list_processes())
        if (info.pid == pid) { p.name_ = info.name; break; }
    if (auto path = process_image_path(pid)) p.image_path_ = widen(*path);
    BOOL wow = FALSE;
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    bool os64 = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 || si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
    p.is64_ = os64 && !(IsWow64Process(h, &wow) && wow);
    return p;
}

Process::Process(Process&& o) noexcept
    : h_(o.h_), pid_(o.pid_), name_(std::move(o.name_)), image_path_(std::move(o.image_path_)), is64_(o.is64_) {
    o.h_ = nullptr;
}

Process& Process::operator=(Process&& o) noexcept {
    if (this != &o) {
        if (h_) CloseHandle(h_);
        h_ = o.h_; pid_ = o.pid_; name_ = std::move(o.name_); image_path_ = std::move(o.image_path_); is64_ = o.is64_;
        o.h_ = nullptr;
    }
    return *this;
}

Process::~Process() { if (h_) CloseHandle(h_); }

bool Process::alive() const {
    return h_ && WaitForSingleObject(h_, 0) == WAIT_TIMEOUT;
}

bool Process::read(uint64_t addr, void* out, size_t n) const {
    SIZE_T got = 0;
    return h_ && ReadProcessMemory(h_, (LPCVOID)(uintptr_t)addr, out, n, &got) && got == n;
}

size_t Process::read_partial(uint64_t addr, void* out, size_t n) const {
    auto* dst = static_cast<uint8_t*>(out);
    size_t done = 0;
    while (done < n) {
        SIZE_T got = 0;
        size_t want = n - done;
        // stop at the end of the current page if the whole read fails
        if (ReadProcessMemory(h_, (LPCVOID)(uintptr_t)(addr + done), dst + done, want, &got) && got == want) { done += want; break; }
        done += got;
        if (got == 0) break;
    }
    return done;
}

bool Process::write(uint64_t addr, const void* data, size_t n) const {
    if (!h_) return false;
    SIZE_T put = 0;
    if (WriteProcessMemory(h_, (LPVOID)(uintptr_t)addr, data, n, &put) && put == n) return true;
    // Read-only / executable page: lift protection for the write, then put it back (like other memory tools do).
    DWORD old = 0;
    if (!VirtualProtectEx(h_, (LPVOID)(uintptr_t)addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    bool ok = WriteProcessMemory(h_, (LPVOID)(uintptr_t)addr, data, n, &put) && put == n;
    DWORD tmp = 0;
    VirtualProtectEx(h_, (LPVOID)(uintptr_t)addr, n, old, &tmp);
    FlushInstructionCache(h_, (LPCVOID)(uintptr_t)addr, n);
    return ok;
}

std::optional<uint64_t> Process::read_pointer(uint64_t addr) const {
    if (is64_) {
        uint64_t v = 0;
        if (!read(addr, &v, 8)) return std::nullopt;
        return v;
    }
    uint32_t v = 0;
    if (!read(addr, &v, 4)) return std::nullopt;
    return (uint64_t)v;
}

std::vector<Region> Process::regions(const RegionFilter& f) const {
    std::vector<Region> out;
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    uint64_t max_addr = is64_ ? (uint64_t)(uintptr_t)si.lpMaximumApplicationAddress : 0xFFFFFFFFull;
    uint64_t addr = f.lo;
    MEMORY_BASIC_INFORMATION mbi{};
    while (addr < f.hi && addr <= max_addr && VirtualQueryEx(h_, (LPCVOID)(uintptr_t)addr, &mbi, sizeof mbi) == sizeof mbi) {
        uint64_t base = (uint64_t)(uintptr_t)mbi.BaseAddress, size = mbi.RegionSize;
        uint64_t next = base + size;
        if (next <= addr) break;                          // paranoia: guarantee forward progress
        addr = next;
        if (mbi.State != MEM_COMMIT) continue;
        DWORD p = mbi.Protect;
        if ((p & (PAGE_GUARD | PAGE_NOACCESS)) || p == 0) continue;
        Region r;
        r.base = base; r.size = size; r.protect = p;
        r.kind = mbi.Type == MEM_IMAGE ? Region::Kind::Image : mbi.Type == MEM_MAPPED ? Region::Kind::Mapped : Region::Kind::Private;
        r.writable = (p & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
        r.executable = (p & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
        r.readable = (p & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
        if (!r.readable) continue;
        if (f.writable_only && !r.writable) continue;
        if (r.executable && !f.include_executable) continue;
        if (r.kind == Region::Kind::Private && !f.include_private) continue;
        if (r.kind == Region::Kind::Image && !f.include_image) continue;
        if (r.kind == Region::Kind::Mapped && !f.include_mapped) continue;
        // clip to the requested window
        if (r.base < f.lo) { r.size -= f.lo - r.base; r.base = f.lo; }
        if (r.base + r.size > f.hi) r.size = f.hi > r.base ? f.hi - r.base : 0;
        if (r.size) out.push_back(r);
    }
    return out;
}

std::optional<ModuleInfo> Process::main_module() const {
    auto mods = modules();
    if (mods.empty()) return std::nullopt;
    for (auto& m : mods)
        if (iequals(m.name, name_)) return m;
    return mods.front();   // the first module is the exe
}

std::optional<ModuleInfo> Process::module(std::string_view n) const {
    for (auto& m : modules())
        if (iequals(m.name, n)) return m;
    return std::nullopt;
}

Result<std::vector<Section>> Process::sections(uint64_t base) const {
    uint8_t hdr[0x1000] = {};
    if (read_partial(base, hdr, sizeof hdr) < 0x200) return Error{"cannot read the module header"};
    if (hdr[0] != 'M' || hdr[1] != 'Z') return Error{"not a PE image"};
    uint32_t pe = 0;
    std::memcpy(&pe, hdr + 0x3C, 4);
    if (pe + 24 > sizeof hdr || std::memcmp(hdr + pe, "PE\0\0", 4) != 0) return Error{"not a PE image"};
    uint16_t nsec = 0, opt = 0;
    std::memcpy(&nsec, hdr + pe + 6, 2);
    std::memcpy(&opt, hdr + pe + 20, 2);          // size of optional header (differs for PE32 / PE32+)
    size_t off = (size_t)pe + 24 + opt;
    std::vector<Section> out;
    for (uint16_t i = 0; i < nsec && off + 40 <= sizeof hdr; ++i, off += 40) {
        Section s;
        char nm[9] = {};
        std::memcpy(nm, hdr + off, 8);
        s.name = nm;
        uint32_t vsize, rva, flags;
        std::memcpy(&vsize, hdr + off + 8, 4);
        std::memcpy(&rva, hdr + off + 12, 4);
        std::memcpy(&flags, hdr + off + 36, 4);
        s.rva = rva; s.size = vsize; s.flags = flags;
        s.writable = (flags & 0x80000000u) != 0;
        s.executable = (flags & 0x20000000u) != 0;
        out.push_back(std::move(s));
    }
    return out;
}

}  // namespace hx
