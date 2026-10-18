#pragma once
// The one process the user explicitly attached to. Everything that reads or writes game memory goes through here.
#include <memory>
#include <optional>
#include <string>

#include "core/process.h"
#include "core/result.h"

namespace hx {

class Session {
public:
    struct Info {
        uint32_t pid = 0;
        std::string name;
        fs::path image;
        bool is64 = true;
        uint64_t base = 0;           // main module
        uint64_t size = 0;
        std::string exe_sha256;      // identifies the game build
    };

    // Runs the safety check first (multiplayer / anti-cheat -> refuse), then opens the process.
    Status attach(uint32_t pid);
    // For unit tests that attach to the test process itself. The safety check is the only thing skipped.
    Status attach_unchecked(uint32_t pid);
    void detach();

    [[nodiscard]] bool attached() const { return proc_ != nullptr; }
    // False once the process has exited (the session then detaches itself).
    bool alive();
    [[nodiscard]] std::shared_ptr<Process> process() const { return proc_; }
    [[nodiscard]] const std::optional<Info>& info() const { return info_; }

private:
    Status open(uint32_t pid, bool check);
    std::shared_ptr<Process> proc_;
    std::optional<Info> info_;
};

}  // namespace hx
