#include "core/console_driver.h"

#include <windows.h>

#include <chrono>
#include <thread>

#include "core/util.h"

namespace hx {

namespace {

void sleep_s(double s) { std::this_thread::sleep_for(std::chrono::duration<double>(s)); }

void key(WORD scan, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = scan;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
    SendInput(1, &in, sizeof in);
}

void tap(WORD scan) { key(scan, false); sleep_s(0.03); key(scan, true); }

void unicode_char(wchar_t ch, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = ch;
    in.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
    SendInput(1, &in, sizeof in);
}

void type_text(const std::string& text, double delay) {
    for (wchar_t ch : widen(text)) {
        unicode_char(ch, false);
        sleep_s(delay);
        unicode_char(ch, true);
        sleep_s(delay);
    }
}

std::optional<WORD> scan_for(const std::string& name) {
    static const std::pair<const char*, WORD> table[] = {
        {"esc", 0x01}, {"enter", 0x1C}, {"ctrl", 0x1D}, {"shift", 0x2A}, {"alt", 0x38}, {"space", 0x39}, {"backtick", 0x29}, {"tab", 0x0F},
        {"a", 0x1E}, {"b", 0x30}, {"c", 0x2E}, {"d", 0x20}, {"e", 0x12}, {"f", 0x21}, {"g", 0x22}, {"h", 0x23}, {"i", 0x17},
        {"j", 0x24}, {"k", 0x25}, {"l", 0x26}, {"m", 0x32}, {"n", 0x31}, {"o", 0x18}, {"p", 0x19}, {"q", 0x10}, {"r", 0x13},
        {"s", 0x1F}, {"t", 0x14}, {"u", 0x16}, {"v", 0x2F}, {"w", 0x11}, {"x", 0x2D}, {"y", 0x15}, {"z", 0x2C},
    };
    std::string n = lower(name);
    for (auto& [k, v] : table) if (n == k) return v;
    return std::nullopt;
}

struct FindCtx {
    uint32_t pid;
    std::string title;
    HWND found = nullptr;
};

BOOL CALLBACK enum_proc(HWND h, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindCtx*>(lp);
    if (!IsWindowVisible(h)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != ctx->pid) return TRUE;
    wchar_t buf[256];
    int n = GetWindowTextW(h, buf, 256);
    if (n <= 0) return TRUE;
    if (!ctx->title.empty() && lower(narrow(std::wstring_view(buf, n))).find(ctx->title) == std::string::npos) return TRUE;
    ctx->found = h;
    return FALSE;
}

}  // namespace

void* find_window_for_pid(uint32_t pid, const std::string& title_contains) {
    FindCtx ctx{pid, lower(title_contains)};
    EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

bool focus_window(void* hwnd_v) {
    HWND hwnd = static_cast<HWND>(hwnd_v);
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    HWND fg = GetForegroundWindow();
    if (fg == hwnd) return true;
    DWORD cur = GetCurrentThreadId();
    DWORD fg_tid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    bool attached = fg_tid && fg_tid != cur && AttachThreadInput(cur, fg_tid, TRUE);
    SetForegroundWindow(hwnd);
    if (attached) AttachThreadInput(cur, fg_tid, FALSE);
    sleep_s(0.12);
    return GetForegroundWindow() == hwnd;
}

ConsoleDriver::ConsoleDriver(ConsoleConfig cfg, std::string window_title, Ready ready, bool dry_run)
    : cfg_(std::move(cfg)), title_(std::move(window_title)), ready_(std::move(ready)), dry_run_(dry_run) {}

Status ConsoleDriver::send(const std::vector<std::string>& lines) {
    if (lines.empty()) return ok_status();
    if (ready_) {
        auto [ok, why] = ready_();
        if (!ok) return Error{why};
    }
    if (dry_run_) {
        for (auto& l : lines) { dry_log_.push_back(l); log_info("[dry-run] > " + l); }
        return ok_status();
    }
    if (!hwnd_ || !IsWindow(static_cast<HWND>(hwnd_))) hwnd_ = find_window_for_pid(pid_, title_);
    if (!hwnd_) return Error{"The game window was not found (is it minimised or still loading?)."};
    if (!focus_window(hwnd_)) return Error{"Could not bring the game window to the front (is it minimised, or running as administrator?)."};

    auto toggle = [&] {
        if (!cfg_.toggle_combo.empty()) {
            std::vector<WORD> scans;
            for (auto& name : cfg_.toggle_combo) {
                auto s = scan_for(name);
                if (!s) return;
                scans.push_back(*s);
            }
            for (WORD s : scans) { key(s, false); sleep_s(0.01); }
            sleep_s(0.04);
            for (auto it = scans.rbegin(); it != scans.rend(); ++it) key(*it, true);
        } else {
            tap((WORD)cfg_.toggle_scancode);
        }
    };
    toggle();
    sleep_s(cfg_.open_delay);
    for (auto& line : lines) {
        type_text(line, cfg_.type_delay);
        sleep_s(0.05);
        tap(0x1C);                                   // Enter
        sleep_s(cfg_.line_delay);
        log_info("> " + line);
    }
    if (cfg_.close_after) { toggle(); sleep_s(0.08); }
    return ok_status();
}

}  // namespace hx
