#pragma once
// Runtime for one game's trainer definition: resolves symbols, gates cheats, applies memory/console cheats,
// keeps locks, restores values.
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "core/console_driver.h"
#include "core/definition.h"
#include "core/pointer.h"
#include "core/process.h"
#include "core/result.h"

namespace hx {

class Trainer {
public:
    struct Options {
        bool dry_run = false;           // console cheats are logged instead of typed
    };
    struct HookReport {
        std::map<std::string, uint64_t> resolved;        // symbol -> address (0 for pointer-chain symbols)
        std::map<std::string, std::string> unresolved;   // symbol -> why
        std::string warning;                             // e.g. "this game build is not one the trainer was written for"
    };
    struct Outcome {
        bool ok = false;
        bool on = false;                // toggles: the state now in effect
        double value = 0;               // numbers: the value that was applied (after clamping)
        std::string error;              // why a request was refused
    };

    explicit Trainer(GameDef def, Options opt = {});
    ~Trainer();
    Trainer(const Trainer&) = delete;
    Trainer& operator=(const Trainer&) = delete;

    [[nodiscard]] const GameDef& def() const { return def_; }

    // Resolve every symbol against the attached process. `exe_sha256` identifies the build (see GameDef::exe_sha256).
    Result<HookReport> hook(std::shared_ptr<Process> proc, const std::string& exe_sha256 = "");
    // Restores memory toggles (original or off value), clears all toggles, releases the process.
    void unhook();
    [[nodiscard]] bool hooked() const;
    [[nodiscard]] HookReport report() const;
    void set_on_exit(std::function<void()> fn);              // called (on a worker thread) when the game exits while hooked

    // Every cheat needs the game hooked; console cheats also need the console openable.
    [[nodiscard]] std::optional<std::string> gate(const CheatDef& c) const;
    [[nodiscard]] std::pair<bool, std::string> console_ready() const;

    Outcome set_toggle(const std::string& id, bool on);
    Outcome set_number(const std::string& id, double value, const std::string& mode = "set");
    Outcome press(const std::string& id);                    // console buttons (launch buttons are handled by the app)
    void set_option(const std::string& id, const std::string& option, double value);
    void set_console_combo(std::vector<std::string> combo);  // user override of the console key

    [[nodiscard]] bool toggle_on(const std::string& id) const;
    [[nodiscard]] std::map<std::string, bool> toggles() const;
    // Read a memory spec (symbol or chain) in the cheat's units (raw / scale).
    [[nodiscard]] std::optional<double> read_memory(const MemorySpec& m, std::string* error = nullptr) const;

private:
    struct Sym {
        ValueType type = ValueType::U8;
        std::vector<int64_t> deref;
        bool dynamic = false;
        PointerChain chain;
        uint64_t addr = 0;
    };

    Result<uint64_t> address_of(const MemorySpec& m, ValueType& type) const;
    bool write_memory(const MemorySpec& m, double value, std::string* error);
    double off_value(const CheatDef& c);
    Outcome deny(const CheatDef& c, const std::string& why);
    void enqueue_console(const CheatDef& c, std::vector<std::string> lines);
    void worker();
    void executor();
    void handle_exit();

    GameDef def_;
    Options opt_;
    mutable std::recursive_mutex mu_;
    std::shared_ptr<Process> proc_;
    std::map<std::string, Sym> syms_;
    HookReport report_;
    std::map<std::string, bool> toggles_;
    std::map<std::string, double> orig_;
    std::map<std::string, std::map<std::string, double>> opts_;
    std::map<std::string, int64_t> lock_last_, repeat_last_;
    std::function<void()> on_exit_;

    std::unique_ptr<ConsoleDriver> console_;
    std::atomic<bool> stop_{false};
    std::thread worker_, executor_;
    std::mutex q_mu_;
    std::condition_variable q_cv_;
    std::deque<std::function<void()>> queue_;
};

}  // namespace hx
