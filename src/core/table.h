#pragma once
// Address list (Cheat Engine style): named addresses you can read, edit, freeze, and save per game.
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/pointer.h"
#include "core/process.h"
#include "core/value.h"

namespace hx {

struct TableEntry {
    std::string label;
    ValueType type = ValueType::I32;
    PointerChain address;               // module+offset[,offsets...] so it survives restarts; module "" = absolute address
    bool frozen = false;
    Value freeze_value;                 // written repeatedly while frozen
    std::string note;
};

class AddressTable {
public:
    AddressTable() = default;

    size_t size() const;
    std::vector<TableEntry> snapshot() const;               // a copy for drawing (the table is shared with the freeze thread)
    size_t add(TableEntry e);
    void remove(size_t index);
    void clear();
    void rename(size_t index, std::string label);
    void set_type(size_t index, ValueType t);               // also re-types the frozen value

    // Typed access in the target. Chains are resolved on every call.
    std::optional<Value> read(const Process& p, size_t index, std::string* error = nullptr) const;
    bool write(const Process& p, size_t index, const Value& v, std::string* error = nullptr);
    bool set_frozen(size_t index, bool frozen, std::optional<Value> value = std::nullopt);
    // One freeze pass (also what the background thread calls). Returns the number of values written.
    size_t apply_frozen(const Process& p);

    // Persistence: {"version":1,"exe":..., "exe_sha256":..., "entries":[...]}
    [[nodiscard]] std::string to_json(const std::string& exe_name, const std::string& exe_sha256) const;
    // Returns an error message on failure; `build_warning` is set when the table was saved for another exe hash.
    std::string load_json(const std::string& text, const std::string& current_exe_sha256, std::string* build_warning = nullptr);
    bool save(const fs::path& path, const std::string& exe_name, const std::string& exe_sha256) const;
    std::string load(const fs::path& path, const std::string& current_exe_sha256, std::string* build_warning = nullptr);

    // Default location: %APPDATA%\Hexwright\tables\<exe name>.json
    static fs::path default_path(const std::string& exe_name);

private:
    mutable std::mutex mu_;
    std::vector<TableEntry> entries_;
};

// Background thread that re-applies frozen values (default every 50 ms) until stopped or the process exits.
class FreezeEngine {
public:
    FreezeEngine() = default;
    ~FreezeEngine() { stop(); }
    FreezeEngine(const FreezeEngine&) = delete;
    FreezeEngine& operator=(const FreezeEngine&) = delete;
    void start(std::shared_ptr<Process> proc, AddressTable* table, int interval_ms = 50);
    void stop();
    [[nodiscard]] bool running() const { return thread_.joinable(); }

private:
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}  // namespace hx
