#pragma once
// Global hotkeys (RegisterHotKey on a dedicated message-loop thread).
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace hx {

struct Combo {
    uint32_t mods = 0;      // MOD_ALT=1, MOD_CONTROL=2, MOD_SHIFT=4, MOD_WIN=8
    uint32_t vk = 0;
};

// "Ctrl+Shift+F5", "Alt+A", "Num5", "`" ... nullopt if it is not a valid combination (a lone modifier is invalid).
std::optional<Combo> parse_combo(const std::string& text);
std::string format_combo(const Combo& c);

class HotkeyManager {
public:
    HotkeyManager();
    ~HotkeyManager();
    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    // Returns the id, or nullopt with `error` set ("already used by another program", ...).
    std::optional<int> add(const std::string& combo, std::function<void()> handler, std::string* error = nullptr);
    void clear();                       // unregister everything
    void simulate(int id);              // fire a handler as if the key was pressed (tests)

private:
    void loop();
    template <class F> auto run_on_loop(F&& f) -> decltype(f());

    std::thread thread_;
    std::atomic<unsigned long> thread_id_{0};
    std::mutex mu_;
    std::map<int, std::function<void()>> handlers_;
    int next_id_ = 1;
};

}  // namespace hx
