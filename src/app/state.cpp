#include "app/state.h"

#include <shellapi.h>

#include <algorithm>

#include "core/hash.h"

namespace hx {

namespace {
Library make_library() { return Library::with_defaults(); }
}  // namespace

AppState::AppState()
    : settings(), store(std::make_unique<GameStore>(GameStore::default_config())), library(make_library()),
      catalog(std::make_unique<Catalog>(Catalog::default_config())) {}

AppState::~AppState() { stop(); }

void AppState::start() {
    monitor_ = std::thread([this] { monitor_loop(); });
    std::string last = settings.last_game();
    if (!last.empty() && store->find(last)) select_game(last);
    else if (!store->games().empty()) select_game(store->games().front().def.id);
    rescan_library(false);
    if (settings.app().autoupdate) check_update(true);
}

void AppState::stop() {
    if (stopping_.exchange(true)) return;
    scan_progress.cancel = true;
    hotkeys_.clear();
    freezer.stop();
    if (monitor_.joinable()) monitor_.join();
    std::vector<std::thread> ws;
    {
        std::lock_guard l(workers_mu_);
        ws.swap(workers_);
    }
    for (auto& t : ws) if (t.joinable()) t.join();
    if (trainer_) trainer_->unhook();
    trainer_.reset();
}

void AppState::run_async(std::function<void()> fn) {
    std::lock_guard l(workers_mu_);
    // Reap finished threads lazily by joining them only at shutdown; keep the vector small by reusing slots.
    workers_.emplace_back([f = std::move(fn)] { f(); });
}

void AppState::post_ui(std::function<void()> fn) {
    std::lock_guard l(ui_mu_);
    ui_queue_.push_back(std::move(fn));
}

void AppState::frame() {
    std::deque<std::function<void()>> q;
    {
        std::lock_guard l(ui_mu_);
        q.swap(ui_queue_);
        int64_t now = now_ms();
        notices_.erase(std::remove_if(notices_.begin(), notices_.end(), [&](const Notice& n) { return n.until_ms < now; }), notices_.end());
    }
    for (auto& f : q) f();
}

void AppState::notify(const std::string& text, bool bad) {
    log(bad ? Level::Warn : Level::Info, text);
    std::lock_guard l(ui_mu_);
    notices_.push_back({text, bad, now_ms() + (bad ? 7000 : 4000)});
    if (notices_.size() > 4) notices_.erase(notices_.begin());
}

std::vector<Notice> AppState::notices() {
    std::lock_guard l(ui_mu_);
    return notices_;
}

// ---------------------------------------------------------------- games
const LoadedGame* AppState::current() const { return current_id_.empty() ? nullptr : store->find(current_id_); }
std::shared_ptr<Trainer> AppState::trainer() const { return trainer_; }
std::string AppState::current_id() const { return current_id_; }

bool AppState::select_game(const std::string& id) {
    const LoadedGame* g = store->find(id);
    if (!g) return false;
    if (id == current_id_ && trainer_) return true;
    if (trainer_) trainer_->unhook();
    hotkeys_.clear();
    trainer_ = std::make_shared<Trainer>(g->def);
    current_id_ = id;
    hook_message.clear();
    build_warning.clear();
    settings.set_last_game(id);
    // Re-attach the trainer to an already attached game process (e.g. after switching away and back)
    rebuild_hotkeys();
    return true;
}

uint32_t AppState::running_pid(const LoadedGame& g) const {
    auto* self = const_cast<AppState*>(this);
    std::lock_guard l(self->mu);
    auto it = running.find(lower(g.def.exe));
    return it == running.end() ? 0 : it->second;
}

void AppState::monitor_loop() {
    int tick = 0;
    while (!stopping_) {
        for (int i = 0; i < 10 && !stopping_; ++i) Sleep(100);
        if (stopping_) break;
        auto procs = list_processes();
        std::map<std::string, uint32_t> now;
        for (auto& p : procs) now.emplace(lower(p.name), p.pid);
        {
            std::lock_guard l(mu);
            running = std::move(now);
        }
        // The attached process went away
        bool gone = false;
        {
            std::lock_guard l(sess_mu_);
            if (session.attached() && !session.alive()) {
                gone = true;
                session.detach();
            }
        }
        if (gone) {
            freezer.stop();
            std::string name;
            {
                std::lock_guard l(mu);
                if (attached) name = attached->name;
                attached.reset();
            }
            if (trainer_) trainer_->unhook();
            notify((name.empty() ? "The game" : name) + " exited. Trainer released.", false);
        }
        // Auto-hook the selected game when it appears
        if (settings.app().auto_hook && !hooking && ++tick >= 1) {
            const LoadedGame* g = current();
            if (g && trainer_ && !trainer_->hooked()) {
                uint32_t pid = running_pid(*g);
                if (pid && auto_failed_[g->def.id] != pid) {
                    auto_failed_[g->def.id] = pid;     // one automatic attempt per process
                    post_ui([this] { hook_current(); });
                }
            }
        }
    }
}

void AppState::hook_current() {
    const LoadedGame* g = current();
    if (!g || !trainer_ || hooking) return;
    hooking = true;
    hook_message.clear();
    GameDef def = g->def;
    auto tr = trainer_;
    run_async([this, def, tr] {
        auto finish = [&](const std::string& msg, bool bad) {
            hook_message = msg;
            hook_message_bad = bad;
            if (bad) notify(msg, true);
            hooking = false;
        };
        auto pids = find_pids(def.exe);
        if (pids.empty()) return finish(def.name + " isn't running. Press Play first.", true);
        uint32_t pid = pids.front();
        std::shared_ptr<Process> proc;
        std::string sha;
        {
            std::lock_guard l(sess_mu_);
            if (session.attached() && session.info()->pid != pid) {
                freezer.stop();
                session.detach();
            }
            if (!session.attached()) {
                auto st = session.attach(pid);
                if (!st.ok()) {
                    {
                        std::lock_guard l2(mu);
                        attached.reset();
                    }
                    return finish(st.message(), true);
                }
            }
            proc = session.process();
            sha = session.info()->exe_sha256;
            std::lock_guard l2(mu);
            attached = session.info();
        }
        auto r = tr->hook(proc, sha);
        if (!r.ok()) return finish(r.message(), true);
        build_warning = r->warning;
        if (!r->unresolved.empty()) {
            std::string m = "Hooked, but some parts of the game weren't found";
            for (auto& [k, v] : r->unresolved) { m += "\n- " + k + ": " + v; }
            hook_message = m;
            hook_message_bad = true;
        } else {
            hook_message = "Hooked " + def.name + ".";
            hook_message_bad = false;
        }
        tr->set_on_exit([this] {
            {
                std::lock_guard l(sess_mu_);
                session.detach();
            }
            freezer.stop();
            {
                std::lock_guard l(mu);
                attached.reset();
            }
            notify("The game exited. Trainer released.", false);
        });
        settings.set_last_played(def.id, unix_seconds());
        rebuild_hotkeys();
        if (settings.auto_apply(def.id)) post_ui([this] { apply_preset(); });
        hooking = false;
    });
}

void AppState::unhook_current() {
    if (trainer_) trainer_->unhook();
    hook_message.clear();
    build_warning.clear();
    // The auto-hook must not immediately re-hook the same process
    if (const LoadedGame* g = current()) auto_failed_[g->def.id] = running_pid(*g);
}

void AppState::launch_current() {
    const LoadedGame* g = current();
    if (!g) return;
    std::vector<LibraryGame> det;
    {
        std::lock_guard l(mu);
        det = detected;
    }
    const LibraryGame* lg = find_game(det, g->def.match_spec());
    fs::path exe;
    if (lg && !lg->install_dir.empty()) {
        std::error_code ec;
        std::wstring want = widen(lower(g->def.exe));
        for (auto it = fs::recursive_directory_iterator(lg->install_dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (it.depth() > 3) { it.disable_recursion_pending(); continue; }
            if (!it->is_regular_file(ec)) continue;
            std::wstring n = it->path().filename().wstring();
            std::transform(n.begin(), n.end(), n.begin(), ::towlower);
            if (n == want) { exe = it->path(); break; }
        }
    }
    HINSTANCE r;
    if (!exe.empty()) {
        std::wstring args = widen(g->def.launch_args);
        r = ShellExecuteW(nullptr, L"open", exe.wstring().c_str(), args.empty() ? nullptr : args.c_str(), exe.parent_path().wstring().c_str(), SW_SHOWNORMAL);
    } else if (g->def.steam_appid) {
        r = ShellExecuteW(nullptr, L"open", widen("steam://rungameid/" + std::to_string(*g->def.steam_appid)).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        notify("Couldn't find " + g->def.exe + " to launch " + g->def.name + ". Start the game yourself, then press Hook.", true);
        return;
    }
    if ((INT_PTR)r <= 32) notify("Couldn't start " + g->def.name + ".", true);
    else notify("Launching " + g->def.name + "...", false);
}

void AppState::toggle_cheat(const CheatDef& c, bool on) {
    if (!trainer_) return;
    auto o = trainer_->set_toggle(c.id, on);
    if (!o.ok) notify(o.error, true);
}

void AppState::apply_number(const CheatDef& c, double v, const std::string& mode) {
    if (!trainer_) return;
    auto o = trainer_->set_number(c.id, v, mode);
    if (!o.ok) notify(o.error, true);
    else if (const LoadedGame* g = current()) settings.remember_value(g->def.id, c.id, v, mode);
}

void AppState::press_cheat(const CheatDef& c) {
    if (!trainer_) return;
    if (c.launch) { launch_current(); return; }
    auto o = trainer_->press(c.id);
    if (!o.ok) notify(o.error, true);
}

void AppState::rebuild_hotkeys() {
    hotkeys_.clear();
    AppSettings as = settings.app();
    std::string err;
    hotkeys_.add(as.overlay_hotkey, [this] { overlay_wanted = !overlay_wanted; }, &err);
    const LoadedGame* g = current();
    if (!g || !trainer_ || !as.hotkeys_enabled) return;
    auto tr = trainer_;
    for (auto& [cheat_id, combo] : settings.hotkeys(g->def.id)) {
        const CheatDef* c = g->def.find_cheat(cheat_id);
        if (!c || combo.empty()) continue;
        std::string e;
        auto id = hotkeys_.add(combo, [this, tr, cid = cheat_id] {
            const CheatDef* cd = tr->def().find_cheat(cid);
            if (!cd) return;
            if (cd->kind == CheatKind::Toggle) toggle_cheat(*cd, !tr->toggle_on(cid));
            else if (cd->kind == CheatKind::Button) press_cheat(*cd);
            else if (const LoadedGame* cur = current()) apply_number(*cd, settings.value(cur->def.id, cid, cd->def), settings.mode(cur->def.id, cid).empty() ? "set" : settings.mode(cur->def.id, cid));
        }, &e);
        if (!id) notify("Hotkey " + combo + " for " + c->label + ": " + e, true);
    }
}

void AppState::save_preset() {
    const LoadedGame* g = current();
    if (!g || !trainer_) return;
    Preset p;
    for (auto& [id, on] : trainer_->toggles()) if (on) p.toggles.push_back(id);
    for (auto& c : g->def.cheats) {
        if (c.kind != CheatKind::Number) continue;
        std::string m = settings.mode(g->def.id, c.id);
        p.numbers[c.id] = {settings.value(g->def.id, c.id, c.def), m.empty() ? "set" : m};
    }
    settings.set_preset(g->def.id, p);
    notify("Saved your current cheats as this game's preset.");
}

void AppState::apply_preset() {
    const LoadedGame* g = current();
    if (!g || !trainer_ || !trainer_->hooked()) return;
    auto p = settings.preset(g->def.id);
    if (!p) return;
    auto tr = trainer_;
    GameDef def = g->def;
    run_async([this, tr, def, p = *p] {
        for (auto& id : p.toggles) {
            const CheatDef* c = def.find_cheat(id);
            if (c && !tr->toggle_on(id)) toggle_cheat(*c, true);
        }
        for (auto& [id, vm] : p.numbers) {
            const CheatDef* c = def.find_cheat(id);
            if (c && !tr->gate(*c)) apply_number(*c, vm.first, vm.second);
        }
    });
}

std::set<std::string> AppState::active_cheat_labels() const {
    std::set<std::string> out;
    const LoadedGame* g = current();
    if (!g || !trainer_) return out;
    for (auto& [id, on] : trainer_->toggles()) {
        if (!on) continue;
        if (const CheatDef* c = g->def.find_cheat(id)) out.insert(c->label);
    }
    return out;
}

// ------------------------------------------------------------ process picker
void AppState::refresh_processes() {
    auto p = list_processes();
    std::sort(p.begin(), p.end(), [](const ProcessInfo& a, const ProcessInfo& b) { return lower(a.name) < lower(b.name); });
    std::lock_guard l(mu);
    processes = std::move(p);
}

void AppState::attach_pid(uint32_t pid) {
    if (attaching) return;
    attaching = true;
    attach_error.clear();
    run_async([this, pid] {
        freezer.stop();
        if (trainer_ && trainer_->hooked()) trainer_->unhook();   // before taking sess_mu_: its exit callback needs it
        std::lock_guard l(sess_mu_);
        session.detach();
        auto st = session.attach(pid);
        {
            std::lock_guard l2(mu);
            if (st.ok()) {
                attached = session.info();
                attach_error.clear();
            } else {
                attached.reset();
                attach_error = st.message();
            }
        }
        if (st.ok()) {
            freezer.start(session.process(), &table);
            scanner.reset();
            notify("Attached to " + session.info()->name + " (PID " + std::to_string(pid) + ").");
        } else {
            notify(st.message(), true);
        }
        attaching = false;
    });
}

void AppState::detach() {
    scan_progress.cancel = true;
    freezer.stop();
    if (trainer_ && trainer_->hooked()) trainer_->unhook();
    std::lock_guard l(sess_mu_);
    session.detach();
    std::lock_guard l2(mu);
    attached.reset();
}

// ------------------------------------------------------------------ scanner
ScanView AppState::scan_view() {
    ScanView v;
    v.busy = scan_busy_;
    v.progress = scan_progress.fraction();
    v.error = scan_error_;
    v.status = scan_status_;
    if (!v.busy) {
        v.has_scan = scanner.has_scan();
        v.count = scanner.count();
        v.snapshot = scanner.is_snapshot();
    }
    return v;
}

std::vector<Hit> AppState::scan_results(size_t max) {
    std::vector<Hit> out;
    if (scan_busy_) return out;
    auto& h = scanner.hits();
    out.assign(h.begin(), h.begin() + std::min(max, h.size()));
    return out;
}

void AppState::first_scan(const ScanParams& p) {
    if (scan_busy_) return;
    std::shared_ptr<Process> proc;
    {
        std::lock_guard l(sess_mu_);
        proc = session.process();
    }
    if (!proc) { scan_error_ = "Attach to a process first."; return; }
    scan_busy_ = true;
    scan_error_.clear();
    scan_progress.cancel = false;
    scan_progress.done_bytes = 0;
    scan_progress.total_bytes = 0;
    ScanConfig cfg = scan_cfg;
    run_async([this, proc, cfg, p] {
        scanner.reset();
        auto r = scanner.first_scan(*proc, cfg, p, &scan_progress);
        if (!r.ok()) scan_error_ = r.message();
        else scan_status_ = std::to_string(*r) + (scanner.is_snapshot() ? " values remembered" : " matches");
        scan_busy_ = false;
    });
}

void AppState::next_scan(const ScanParams& p) {
    if (scan_busy_) return;
    std::shared_ptr<Process> proc;
    {
        std::lock_guard l(sess_mu_);
        proc = session.process();
    }
    if (!proc) { scan_error_ = "Attach to a process first."; return; }
    scan_busy_ = true;
    scan_error_.clear();
    scan_progress.cancel = false;
    scan_progress.done_bytes = 0;
    scan_progress.total_bytes = 0;
    run_async([this, proc, p] {
        auto r = scanner.next_scan(*proc, p, &scan_progress);
        if (!r.ok()) scan_error_ = r.message();
        else scan_status_ = std::to_string(*r) + " matches";
        scan_busy_ = false;
    });
}

void AppState::reset_scan() {
    if (scan_busy_) return;
    scanner.reset();
    scan_error_.clear();
    scan_status_.clear();
}

void AppState::table_load() {
    std::shared_ptr<Process> proc;
    std::optional<Session::Info> inf;
    {
        std::lock_guard l(sess_mu_);
        proc = session.process();
        inf = session.info();
    }
    if (!proc || !inf) return;
    std::string warn;
    std::string err = table.load(AddressTable::default_path(inf->name), inf->exe_sha256, &warn);
    table_message = err.empty() ? (warn.empty() ? "Loaded the saved table." : warn) : err;
}

void AppState::table_save() {
    std::optional<Session::Info> inf;
    {
        std::lock_guard l(sess_mu_);
        inf = session.info();
    }
    if (!inf) return;
    table_message = table.save(AddressTable::default_path(inf->name), inf->name, inf->exe_sha256) ? "Saved the table for " + inf->name + "."
                                                                                                    : "Couldn't save the table.";
}

// ---------------------------------------------------------- library / catalog
void AppState::reload_games() {
    post_ui([this] {
        store->reload();
        if (!current_id_.empty()) {
            if (!store->find(current_id_)) { current_id_.clear(); trainer_.reset(); }
            else if (trainer_ && !trainer_->hooked()) { std::string id = current_id_; current_id_.clear(); select_game(id); }
        }
        std::lock_guard l(mu);
        std::set<std::string> ids;
        for (auto& g : store->games()) ids.insert(g.def.id);
        explore = catalog->entries_for_ui(ids, detected);
    });
}

void AppState::rescan_library(bool force) {
    if (scanning_library) return;
    scanning_library = true;
    run_async([this, force] {
        auto games = library.scan(force);
        {
            std::lock_guard l(mu);
            detected = std::move(games);
            scanning_library = false;
        }
        reload_games();
        if (settings.app().catalog_sync) sync_catalog();
    });
}

void AppState::sync_catalog() {
    if (syncing_catalog) return;
    syncing_catalog = true;
    run_async([this] {
        std::vector<LibraryGame> det;
        {
            std::lock_guard l(mu);
            det = detected;
        }
        catalog->fetch_index();
        auto msgs = catalog->sync(det);
        for (auto& m : msgs) {
            if (!m.empty() && m[0] == '!') notify(m.substr(1), true);
            else notify(m);
        }
        {
            std::lock_guard l(mu);
            catalog_status = catalog->status();
            syncing_catalog = false;
        }
        reload_games();
    });
}

void AppState::check_update(bool silent) {
    if (update_busy) return;
    update_busy = true;
    run_async([this, silent] {
        std::string err;
        auto u = check_for_update(default_updater_config(), &err);
        {
            std::lock_guard l(mu);
            update = u;
            update_status = u ? "Version " + u->version + " is available." : (err.empty() ? "You're up to date." : "Couldn't check: " + err);
        }
        if (u && silent) notify("Hexwright " + u->version + " is available. See Settings to update.");
        update_busy = false;
    });
}

void AppState::apply_update_now() {
    std::optional<UpdateInfo> u;
    {
        std::lock_guard l(mu);
        u = update;
    }
    if (!u || update_busy) return;
    update_busy = true;
    run_async([this, u = *u] {
        {
            std::lock_guard l(mu);
            update_status = "Downloading " + u.version + "...";
        }
        auto cfg = default_updater_config();
        auto r = download_update(cfg, u);
        if (!r.ok()) {
            std::lock_guard l(mu);
            update_status = "Update failed: " + r.message();
            update_busy = false;
            return;
        }
        auto st = apply_update(*r);
        if (!st.ok()) {
            std::lock_guard l(mu);
            update_status = "Update failed: " + st.message();
            update_busy = false;
            return;
        }
        quit = true;
    });
}

}  // namespace hx

namespace hx {
std::shared_ptr<Process> AppState::process() {
    std::lock_guard l(sess_mu_);
    return session.process();
}
}  // namespace hx
