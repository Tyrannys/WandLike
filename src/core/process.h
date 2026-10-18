#pragma once
// Process enumeration and memory access for ONE explicitly chosen target process.
#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.h"
#include "core/util.h"

namespace hx {

struct ProcessInfo {
    uint32_t pid = 0;
    uint32_t parent = 0;
    std::string name;       // exe file name, e.g. "ck3.exe"
};

struct ModuleInfo {
    std::string name;
    std::string path;
    uint64_t base = 0;
    uint64_t size = 0;
};

struct Region {
    enum class Kind { Private, Image, Mapped };
    uint64_t base = 0;
    uint64_t size = 0;
    uint32_t protect = 0;
    Kind kind = Kind::Private;
    bool readable = false, writable = false, executable = false;
};

struct RegionFilter {
    bool writable_only = true;      // Cheat Engine's default: only memory that can hold changing values
    bool include_private = true;    // heap / stack
    bool include_image = true;      // exe and dll data sections
    bool include_mapped = false;    // memory-mapped files
    bool include_executable = false;
    uint64_t lo = 0;
    uint64_t hi = ~0ull;
};

struct Section {
    std::string name;
    uint64_t rva = 0;
    uint64_t size = 0;
    uint32_t flags = 0;
    bool writable = false;
    bool executable = false;
};

std::vector<ProcessInfo> list_processes();
std::vector<uint32_t> find_pids(std::string_view exe_name);          // case-insensitive
std::vector<ModuleInfo> list_modules(uint32_t pid);                  // works for 32 and 64-bit targets
std::optional<std::string> process_image_path(uint32_t pid);

class Process {
public:
    // Least rights that cover read, write, region queries and waiting for exit. Never requests debug or terminate rights.
    static Result<Process> open(uint32_t pid);

    Process(Process&& o) noexcept;
    Process& operator=(Process&& o) noexcept;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    ~Process();

    [[nodiscard]] uint32_t pid() const { return pid_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const fs::path& image_path() const { return image_path_; }
    [[nodiscard]] bool is64() const { return is64_; }
    [[nodiscard]] uint32_t pointer_size() const { return is64_ ? 8 : 4; }
    [[nodiscard]] HANDLE handle() const { return h_; }
    [[nodiscard]] bool alive() const;                                 // false once the process has exited

    bool read(uint64_t addr, void* out, size_t n) const;              // all or nothing
    size_t read_partial(uint64_t addr, void* out, size_t n) const;    // bytes read before the first unreadable page
    bool write(uint64_t addr, const void* data, size_t n) const;      // lifts page protection if needed, then restores it

    template <class T>
    std::optional<T> read_value(uint64_t addr) const {
        T v{};
        if (!read(addr, &v, sizeof v)) return std::nullopt;
        return v;
    }
    template <class T>
    bool write_value(uint64_t addr, const T& v) const { return write(addr, &v, sizeof v); }
    std::optional<uint64_t> read_pointer(uint64_t addr) const;        // 4 or 8 bytes depending on the target

    [[nodiscard]] std::vector<Region> regions(const RegionFilter& f = {}) const;
    [[nodiscard]] std::vector<ModuleInfo> modules() const { return list_modules(pid_); }
    [[nodiscard]] std::optional<ModuleInfo> main_module() const;
    [[nodiscard]] std::optional<ModuleInfo> module(std::string_view name) const;
    Result<std::vector<Section>> sections(uint64_t module_base) const;   // PE32 and PE32+

private:
    Process() = default;
    HANDLE h_ = nullptr;
    uint32_t pid_ = 0;
    std::string name_;
    fs::path image_path_;
    bool is64_ = true;
};

}  // namespace hx
