#pragma once
// Hard safety rules: single-player only. There is deliberately no override.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hx {

struct SafetyVerdict {
    bool allowed = true;
    std::string reason;                 // user-facing explanation when refused
    std::vector<std::string> matches;   // what triggered it (process / module / service names)
};

// Pure, testable core: decide from names only.
//   target     : exe name of the process the user wants to attach to
//   modules    : names of the modules loaded in that process
//   processes  : names of every running process
//   services   : names of running Windows services / kernel drivers
SafetyVerdict evaluate_safety(std::string_view target, const std::vector<std::string>& modules,
                              const std::vector<std::string>& processes, const std::vector<std::string>& services);

// Gathers the live data (Toolhelp snapshots + the Service Control Manager) and calls evaluate_safety.
SafetyVerdict check_attach(uint32_t pid, std::string_view target_name);

// Running Windows services and drivers (names), used by check_attach.
std::vector<std::string> running_service_names();

// Exposed for the UI ("why can't I attach?") and the documentation.
const std::vector<std::string>& blocked_game_exes();
const std::vector<std::string>& anticheat_process_names();
const std::vector<std::string>& anticheat_module_names();
const std::vector<std::string>& anticheat_service_names();

}  // namespace hx
