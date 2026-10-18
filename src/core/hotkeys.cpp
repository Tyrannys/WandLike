#include "core/hotkeys.h"

#include <windows.h>

#include <condition_variable>
#include <deque>

#include "core/util.h"

namespace hx {

namespace {
constexpr UINT WM_HX_RUN = WM_APP + 1;

struct Task {
    std::function<void()> fn;
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
};
}  // namespace

std::optional<Combo> parse_combo(const std::string& text) {
    Combo c;
    bool have_key = false;
    for (auto& raw : split(text, '+')) {
        std::string p = lower(trim(raw));
        if (p.empty()) continue;
        if (p == "alt") c.mods |= MOD_ALT;
        else if (p == "ctrl" || p == "control") c.mods |= MOD_CONTROL;
        else if (p == "shift") c.mods |= MOD_SHIFT;
        else if (p == "win" || p == "windows") c.mods |= MOD_WIN;
        else if (have_key) return std::nullopt;                          // two main keys
        else if (p == "space") { c.vk = VK_SPACE; have_key = true; }
        else if (p == "enter") { c.vk = VK_RETURN; have_key = true; }
        else if (p == "tab") { c.vk = VK_TAB; have_key = true; }
        else if (p == "esc" || p == "escape") { c.vk = VK_ESCAPE; have_key = true; }
        else if (p == "backspace") { c.vk = VK_BACK; have_key = true; }
        else if (p == "insert") { c.vk = VK_INSERT; have_key = true; }
        else if (p == "delete") { c.vk = VK_DELETE; have_key = true; }
        else if (p == "home") { c.vk = VK_HOME; have_key = true; }
        else if (p == "end") { c.vk = VK_END; have_key = true; }
        else if (p == "pageup") { c.vk = VK_PRIOR; have_key = true; }
        else if (p == "pagedown") { c.vk = VK_NEXT; have_key = true; }
        else if (p == "left") { c.vk = VK_LEFT; have_key = true; }
        else if (p == "up") { c.vk = VK_UP; have_key = true; }
        else if (p == "right") { c.vk = VK_RIGHT; have_key = true; }
        else if (p == "down") { c.vk = VK_DOWN; have_key = true; }
        else if (p == "`") { c.vk = VK_OEM_3; have_key = true; }
        else if (p == "-") { c.vk = VK_OEM_MINUS; have_key = true; }
        else if (p == "=") { c.vk = VK_OEM_PLUS; have_key = true; }
        else if (p == "[") { c.vk = VK_OEM_4; have_key = true; }
        else if (p == "]") { c.vk = VK_OEM_6; have_key = true; }
        else if (p == ";") { c.vk = VK_OEM_1; have_key = true; }
        else if (p == "'") { c.vk = VK_OEM_7; have_key = true; }
        else if (p == ",") { c.vk = VK_OEM_COMMA; have_key = true; }
        else if (p == ".") { c.vk = VK_OEM_PERIOD; have_key = true; }
        else if (p == "/") { c.vk = VK_OEM_2; have_key = true; }
        else if (p == "\\") { c.vk = VK_OEM_5; have_key = true; }
        else if (p.size() == 1 && std::isalnum((unsigned char)p[0])) { c.vk = (uint32_t)std::toupper((unsigned char)p[0]); have_key = true; }
        else if (p[0] == 'f' && p.size() >= 2 && std::isdigit((unsigned char)p[1])) {
            int n = std::atoi(p.c_str() + 1);
            if (n < 1 || n > 24) return std::nullopt;
            c.vk = VK_F1 + (uint32_t)n - 1;
            have_key = true;
        } else if (p.rfind("num", 0) == 0 && p.size() == 4 && std::isdigit((unsigned char)p[3])) {
            c.vk = VK_NUMPAD0 + (uint32_t)(p[3] - '0');
            have_key = true;
        } else return std::nullopt;
    }
    if (!have_key) return std::nullopt;
    return c;
}

std::string format_combo(const Combo& c) {
    std::string s;
    if (c.mods & MOD_CONTROL) s += "Ctrl+";
    if (c.mods & MOD_ALT) s += "Alt+";
    if (c.mods & MOD_SHIFT) s += "Shift+";
    if (c.mods & MOD_WIN) s += "Win+";
    if (c.vk >= VK_F1 && c.vk <= VK_F24) s += "F" + std::to_string(c.vk - VK_F1 + 1);
    else if (c.vk >= VK_NUMPAD0 && c.vk <= VK_NUMPAD9) s += "Num" + std::to_string(c.vk - VK_NUMPAD0);
    else if ((c.vk >= '0' && c.vk <= '9') || (c.vk >= 'A' && c.vk <= 'Z')) s += (char)c.vk;
    else if (c.vk == VK_SPACE) s += "Space";
    else if (c.vk == VK_OEM_3) s += "`";
    else s += "VK" + std::to_string(c.vk);
    return s;
}

HotkeyManager::HotkeyManager() {
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    thread_ = std::thread([&] {
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);          // create the thread's message queue
        thread_id_ = GetCurrentThreadId();
        { std::lock_guard l(m); ready = true; }
        cv.notify_all();
        loop();
    });
    std::unique_lock l(m);
    cv.wait(l, [&] { return ready; });
}

HotkeyManager::~HotkeyManager() {
    clear();
    PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
}

void HotkeyManager::loop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_HX_RUN) {
            auto* t = reinterpret_cast<Task*>(msg.lParam);
            t->fn();
            { std::lock_guard l(t->mu); t->done = true; }
            t->cv.notify_all();
        } else if (msg.message == WM_HOTKEY) {
            std::function<void()> fn;
            { std::lock_guard l(mu_); auto it = handlers_.find((int)msg.wParam); if (it != handlers_.end()) fn = it->second; }
            if (fn) std::thread(fn).detach();                                // never block the message loop on a handler
        }
    }
}

template <class F>
auto HotkeyManager::run_on_loop(F&& f) -> decltype(f()) {
    using R = decltype(f());
    R result{};
    Task t;
    t.fn = [&] { result = f(); };
    PostThreadMessageW(thread_id_, WM_HX_RUN, 0, reinterpret_cast<LPARAM>(&t));
    std::unique_lock l(t.mu);
    t.cv.wait_for(l, std::chrono::seconds(3), [&] { return t.done; });
    return result;
}

std::optional<int> HotkeyManager::add(const std::string& combo, std::function<void()> handler, std::string* error) {
    auto c = parse_combo(combo);
    if (!c) { if (error) *error = "unrecognised key combination"; return std::nullopt; }
    int id = run_on_loop([&]() -> int {
        int hid;
        { std::lock_guard l(mu_); hid = next_id_++; }
        if (!RegisterHotKey(nullptr, hid, c->mods | MOD_NOREPEAT, c->vk)) return 0;
        std::lock_guard l(mu_);
        handlers_[hid] = handler;
        return hid;
    });
    if (id == 0) { if (error) *error = "already used by another program"; return std::nullopt; }
    return id;
}

void HotkeyManager::clear() {
    run_on_loop([&]() -> int {
        std::lock_guard l(mu_);
        for (auto& [id, fn] : handlers_) UnregisterHotKey(nullptr, id);
        handlers_.clear();
        return 0;
    });
}

void HotkeyManager::simulate(int id) {
    std::function<void()> fn;
    { std::lock_guard l(mu_); auto it = handlers_.find(id); if (it != handlers_.end()) fn = it->second; }
    if (fn) fn();
}

}  // namespace hx
