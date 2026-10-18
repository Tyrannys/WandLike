#pragma once
// Everything the UI shows and changes. UI code runs on the main thread; slow work (library scan, catalog sync,
// attach/hook, memory scans) runs on worker threads and publishes results here under `mu`.
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "core/catalog.h"
#include "core/games.h"
#include "core/hotkeys.h"
#include "core/library.h"
#include "core/safety.h"
#include "core/scanner.h"
#include "core/session.h"
#include "core/settings.h"
#include "core/table.h"
#include "core/trainer.h"
#include "core/updater.h"

namespace hx {

struct Notice {
    std::string text;
    bool bad = false;
    int64_t until_ms = 0;
};

struct ScanView {
    bool busy = false;
    float progress = 0;
    uint64_t count = 0;
    bool snapshot = false;
    bool has_scan = false;
    std::string error;
    std::string status;
};

class AppState {
public:
    AppState();
    ~AppState();

    // ---- lifecycle
    void start();                        // spawns the background threads
    void stop();
    void frame();                        // once per UI frame: expire notices, run queued UI-thread work

    // ---- shared data (lock `mu` to read the containers directly; helpers below lock for you)
    std::mutex mu;
    Settings settings;
    std::unique_ptr<GameStore> store;
    Library library;
    std::unique_ptr<Catalog> catalog;
    std::vector<LibraryGame> detected;
    std::vector<UiCatalogEntry> explore;
    std::string catalog_status;
    bool scanning_library = false, syncing_catalog = false;
    std::optional<UpdateInfo> update;
    std::string update_status;
    std::atomic<bool> update_busy{false};

    // ---- notices
    void notify(const std::string& text, bool bad = false);
    std::vector<Notice> notices();

    // ---- games and trainer
    bool select_game(const std::string& id);               // creates the Trainer for it
    [[nodiscard]] const LoadedGame* current() const;
    [[nodiscard]] std::shared_ptr<Trainer> trainer() const;
    [[nodiscard]] std::string current_id() const;
    uint32_t running_pid(const LoadedGame& g) const;       // 0 when the game is not running
    void hook_current();                                   // async: attach + hook
    void unhook_current();
    void launch_current();
    void toggle_cheat(const CheatDef& c, bool on);
    void apply_number(const CheatDef& c, double v, const std::string& mode);
    void press_cheat(const CheatDef& c);
    void rebuild_hotkeys();
    void save_preset();
    void apply_preset();
    std::atomic<bool> hooking{false};
    std::string hook_message;                              // last result for the game page
    bool hook_message_bad = false;
    std::string build_warning;                             // exe hash mismatch

    // ---- process picker / session
    Session session;                                       // guarded by sess_mu_; the UI reads `attached` instead
    std::optional<Session::Info> attached;                 // guarded by mu
    std::map<std::string, uint32_t> running;               // lower-case exe name -> pid, guarded by mu
    std::vector<ProcessInfo> processes;
    void refresh_processes();
    void attach_pid(uint32_t pid);                         // async, runs the safety check
    void detach();
    std::shared_ptr<Process> process();                    // the attached process (thread-safe copy), or null
    std::string attach_error;
    SafetyVerdict last_verdict;
    std::atomic<bool> attaching{false};

    // ---- scanner + address list
    Scanner scanner;
    ScanConfig scan_cfg;
    AddressTable table;
    FreezeEngine freezer;
    void first_scan(const ScanParams& p);
    void next_scan(const ScanParams& p);
    void reset_scan();
    void cancel_scan() { scan_progress.cancel = true; }
    ScanView scan_view();
    std::vector<Hit> scan_results(size_t max);             // copy of the first hits
    ScanProgress scan_progress;
    void table_load();
    void table_save();
    std::string table_message;

    // ---- library / catalog / updates
    void rescan_library(bool force);
    void sync_catalog();
    void check_update(bool silent);
    void apply_update_now();
    void reload_games();

    // ---- overlay / misc
    std::atomic<bool> overlay_wanted{false};
    std::atomic<bool> quit{false};
    std::set<std::string> active_cheat_labels() const;

private:
    void monitor_loop();
    void run_async(std::function<void()> fn);
    void post_ui(std::function<void()> fn);

    mutable std::mutex ui_mu_;
    std::deque<std::function<void()>> ui_queue_;
    std::vector<Notice> notices_;
    std::shared_ptr<Trainer> trainer_;
    std::string current_id_;
    std::atomic<bool> scan_busy_{false};
    std::string scan_error_, scan_status_;
    std::thread monitor_;
    std::vector<std::thread> workers_;
    std::mutex workers_mu_;
    std::mutex sess_mu_;
    std::map<std::string, uint32_t> auto_failed_;
    std::atomic<bool> stopping_{false};
    HotkeyManager hotkeys_;
    std::atomic<uint32_t> auto_hook_pid_{0};
    std::map<std::string, std::vector<int>> hotkey_ids_;
};

}  // namespace hx
