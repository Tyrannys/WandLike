#pragma once
// Types commands into a game's own console. Used only after the trainer confirmed the console can be opened,
// otherwise the keystrokes would land in the game as hotkeys.
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/definition.h"
#include "core/result.h"

namespace hx {

class ConsoleDriver {
public:
    using Ready = std::function<std::pair<bool, std::string>()>;     // (ok, reason)

    ConsoleDriver(ConsoleConfig cfg, std::string window_title, Ready ready, bool dry_run = false);

    void set_target_pid(uint32_t pid) { pid_ = pid; hwnd_ = nullptr; }
    void set_toggle_combo(std::vector<std::string> combo) { cfg_.toggle_combo = std::move(combo); }   // user override
    // Blocks while typing (focus game, open console, type each line + Enter, close console).
    Status send(const std::vector<std::string>& lines);
    [[nodiscard]] const std::vector<std::string>& dry_run_log() const { return dry_log_; }

private:
    ConsoleConfig cfg_;
    std::string title_;
    Ready ready_;
    bool dry_run_;
    uint32_t pid_ = 0;
    void* hwnd_ = nullptr;
    std::vector<std::string> dry_log_;
};

// Window helpers (also used by the overlay and tests).
void* find_window_for_pid(uint32_t pid, const std::string& title_contains);
bool focus_window(void* hwnd);

}  // namespace hx
