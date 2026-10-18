#include "core/trainer.h"

#include <algorithm>
#include <chrono>

#include "core/aob.h"

namespace hx {

namespace {
constexpr const char* kNotHooked = "hook the game first (press Hook)";
bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
}  // namespace

Trainer::Trainer(GameDef def, Options opt) : def_(std::move(def)), opt_(opt) {
    console_ = std::make_unique<ConsoleDriver>(def_.console, def_.window_title, [this] { return console_ready(); }, opt_.dry_run);
    worker_ = std::thread([this] { worker(); });
    executor_ = std::thread([this] { executor(); });
}

Trainer::~Trainer() {
    unhook();
    stop_ = true;
    q_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    if (executor_.joinable()) executor_.join();
}

bool Trainer::hooked() const {
    std::lock_guard l(mu_);
    return proc_ != nullptr;
}

Trainer::HookReport Trainer::report() const {
    std::lock_guard l(mu_);
    return report_;
}

void Trainer::set_on_exit(std::function<void()> fn) {
    std::lock_guard l(mu_);
    on_exit_ = std::move(fn);
}

void Trainer::set_console_combo(std::vector<std::string> combo) {
    std::lock_guard l(mu_);
    console_->set_toggle_combo(std::move(combo));
}

// ------------------------------------------------------------------ hooking
Result<Trainer::HookReport> Trainer::hook(std::shared_ptr<Process> proc, const std::string& exe_sha256) {
    if (!proc || !proc->alive()) return Error{def_.name + " is not running."};
    auto main = proc->main_module();
    if (!main) return Error{"Could not read the game's main module."};
    HookReport rep;
    const bool known_build = !def_.exe_sha256.empty() && contains(def_.exe_sha256, lower(exe_sha256));
    if (!def_.exe_sha256.empty() && !exe_sha256.empty() && !known_build)
        rep.warning = "This game build is not one this trainer was written for. Cheats found by signature should still work; the rest may be unavailable.";

    std::map<std::string, Sym> syms;
    for (auto& s : def_.symbols) {
        Sym sym;
        sym.type = s.type;
        sym.deref = s.deref;
        if (!s.chain.empty()) {                                           // resolved on every access
            auto c = PointerChain::parse(s.chain);
            if (!c.ok()) { rep.unresolved[s.name] = c.message(); continue; }
            sym.dynamic = true;
            sym.chain = *c;
            syms[s.name] = sym;
            rep.resolved[s.name] = 0;
            continue;
        }
        const bool rva_trusted = s.rva && ((s.exe_size && *s.exe_size == main->size) || known_build);
        if (!s.aob.empty()) {
            auto pat = Pattern::parse(s.aob);
            if (!pat.ok()) { rep.unresolved[s.name] = pat.message(); continue; }
            auto hits = scan_module(*proc, *main, s.section, *pat, 4);
            if (!hits.ok()) { rep.unresolved[s.name] = hits.message(); continue; }
            if (hits->empty()) {
                if (rva_trusted) { sym.addr = main->base + *s.rva; syms[s.name] = sym; rep.resolved[s.name] = sym.addr; }
                else rep.unresolved[s.name] = "byte pattern not found: this game version isn't supported yet";
                continue;
            }
            if (hits->size() > 1 && s.unique && !s.index) {
                rep.unresolved[s.name] = "byte pattern is ambiguous (" + std::to_string(hits->size()) + " matches); make it longer";
                continue;
            }
            size_t idx = (size_t)std::max(0, s.index.value_or(0));
            if (idx >= hits->size()) { rep.unresolved[s.name] = "pattern index out of range"; continue; }
            uint64_t match = (*hits)[idx];
            if (s.rip) {
                auto t = resolve_rip(*proc, match, s.rip->disp_at, s.rip->insn_len);
                if (!t) { rep.unresolved[s.name] = "could not read the RIP-relative operand"; continue; }
                sym.addr = *t;
            } else {
                sym.addr = match + (uint64_t)s.offset;
            }
            syms[s.name] = sym;
            rep.resolved[s.name] = sym.addr;
        } else if (s.rva) {
            if (rva_trusted) { sym.addr = main->base + *s.rva; syms[s.name] = sym; rep.resolved[s.name] = sym.addr; }
            else rep.unresolved[s.name] = "fixed address is for a different game build (exe size " + std::to_string(main->size) +
                                          "); this version isn't supported yet";
        } else {
            rep.unresolved[s.name] = "symbol has neither a signature, a chain nor an address";
        }
    }
    {
        std::lock_guard l(mu_);
        proc_ = proc;
        syms_ = std::move(syms);
        report_ = rep;
        toggles_.clear();
        orig_.clear();
        console_->set_target_pid(proc->pid());
    }
    log_info("Hooked " + def_.name + " (pid " + std::to_string(proc->pid()) + ", " + std::to_string(rep.resolved.size()) + "/" +
             std::to_string(def_.symbols.size()) + " symbols resolved)");
    for (auto& [n, why] : rep.unresolved) log_warn("symbol '" + n + "': " + why);
    if (!rep.warning.empty()) log_warn(rep.warning);
    return rep;
}

void Trainer::unhook() {
    std::lock_guard l(mu_);
    if (!proc_) return;
    for (auto& c : def_.cheats) {
        if (c.memory && c.kind == CheatKind::Toggle && toggle_on(c.id)) {
            std::string err;
            write_memory(*c.memory, off_value(c), &err);
        }
    }
    toggles_.clear();
    orig_.clear();
    lock_last_.clear();
    repeat_last_.clear();
    proc_.reset();
    syms_.clear();
    report_ = {};
    log_info("Unhooked " + def_.name);
}

void Trainer::handle_exit() {
    std::function<void()> cb;
    {
        std::lock_guard l(mu_);
        if (!proc_) return;
        toggles_.clear();
        orig_.clear();
        proc_.reset();
        syms_.clear();
        report_ = {};
        cb = on_exit_;
    }
    log_info(def_.name + " closed; unhooked");
    if (cb) cb();
}

// ------------------------------------------------------------------- memory
Result<uint64_t> Trainer::address_of(const MemorySpec& m, ValueType& type) const {
    std::lock_guard l(mu_);
    if (!proc_) return Error{kNotHooked};
    if (!m.symbol.empty()) {
        auto it = syms_.find(m.symbol);
        if (it == syms_.end()) {
            auto u = report_.unresolved.find(m.symbol);
            return Error{"'" + m.symbol + "' isn't available: " + (u != report_.unresolved.end() ? u->second : "symbol is not part of this definition")};
        }
        const Sym& s = it->second;
        type = m.type.value_or(s.type);
        uint64_t addr = s.addr;
        if (s.dynamic) {
            auto r = resolve_chain(*proc_, s.chain);
            if (!r.ok()) return r.error();
            addr = *r;
        }
        for (int64_t off : s.deref) {
            auto p = proc_->read_pointer(addr);
            if (!p || *p == 0) return Error{"null pointer while resolving '" + m.symbol + "'"};
            addr = *p + (uint64_t)off;
        }
        return addr;
    }
    auto chain = PointerChain::parse(m.chain);
    if (!chain.ok()) return chain.error();
    type = m.type.value_or(ValueType::I32);
    return resolve_chain(*proc_, *chain);
}

std::optional<double> Trainer::read_memory(const MemorySpec& m, std::string* error) const {
    std::lock_guard l(mu_);
    ValueType t = ValueType::I32;
    auto addr = address_of(m, t);
    if (!addr.ok()) { if (error) *error = addr.message(); return std::nullopt; }
    uint8_t buf[8] = {};
    if (!proc_->read(*addr, buf, type_size(t))) { if (error) *error = "could not read game memory"; return std::nullopt; }
    return Value::from_bytes(t, buf).as_double() / (m.scale != 0 ? m.scale : 1);
}

bool Trainer::write_memory(const MemorySpec& m, double value, std::string* error) {
    std::lock_guard l(mu_);
    ValueType t = ValueType::I32;
    auto addr = address_of(m, t);
    if (!addr.ok()) { if (error) *error = addr.message(); return false; }
    uint8_t buf[8] = {};
    Value::from_double(t, value * m.scale).to_bytes(buf);
    if (!proc_->write(*addr, buf, type_size(t))) { if (error) *error = "could not write game memory"; return false; }
    return true;
}

double Trainer::off_value(const CheatDef& c) {
    const MemorySpec& m = *c.memory;
    if (m.restore) {
        auto it = orig_.find(c.id);
        if (it != orig_.end()) { double v = it->second; orig_.erase(it); return v; }
    }
    return m.off;
}

// ------------------------------------------------------------------- gating
std::pair<bool, std::string> Trainer::console_ready() const {
    if (def_.console.requires_symbol.empty()) return {true, ""};
    MemorySpec spec;
    spec.symbol = def_.console.requires_symbol;
    std::string err;
    auto v = read_memory(spec, &err);
    if (!v) return {false, err == kNotHooked ? std::string(kNotHooked) : err};
    if (*v != 0) return {true, ""};
    return {false, "turn on Enable Console first"};
}

std::optional<std::string> Trainer::gate(const CheatDef& c) const {
    if (c.launch) return std::nullopt;
    if (!hooked()) return std::string(kNotHooked);
    if (!c.memory) {
        auto [ok, why] = console_ready();
        if (!ok) return why;
    }
    return std::nullopt;
}

Trainer::Outcome Trainer::deny(const CheatDef& c, const std::string& why) {
    log_error(c.label + ": " + why);
    Outcome o;
    o.error = why;
    return o;
}

// ------------------------------------------------------------------ actions
void Trainer::enqueue_console(const CheatDef& c, std::vector<std::string> lines) {
    if (lines.empty()) return;
    std::string label = c.label;
    {
        std::lock_guard l(q_mu_);
        queue_.push_back([this, label, lines = std::move(lines)] {
            auto r = console_->send(lines);
            if (!r.ok()) log_error(label + ": " + r.message());
        });
    }
    q_cv_.notify_one();
}

Trainer::Outcome Trainer::set_toggle(const std::string& id, bool on) {
    std::lock_guard l(mu_);
    const CheatDef* c = def_.find_cheat(id);
    if (!c || c->kind != CheatKind::Toggle) { Outcome o; o.error = "unknown toggle \"" + id + "\""; return o; }
    if (!c->available()) { Outcome o; o.error = c->unsupported.empty() ? "Not available" : c->unsupported; return o; }
    if (on) {
        if (auto why = gate(*c)) { toggles_[id] = false; return deny(*c, *why); }
    }
    int64_t now = now_ms();
    if (c->memory) {
        const MemorySpec& m = *c->memory;
        double value;
        if (on) {
            if (m.restore && !orig_.count(id))
                if (auto v = read_memory(m)) orig_[id] = *v;
            value = m.on;
        } else if (hooked()) {
            value = off_value(*c);
        } else {
            toggles_[id] = false;
            Outcome o;
            o.ok = true;
            return o;
        }
        std::string err;
        if (!write_memory(m, value, &err)) { toggles_[id] = false; return deny(*c, err); }
        toggles_[id] = on;
        lock_last_[id] = now;
        Outcome o;
        o.ok = true;
        o.on = on;
        return o;
    }
    toggles_[id] = on;
    Outcome o;
    o.ok = true;
    o.on = on;
    if (c->interval) {
        repeat_last_[id] = now;                              // first repeat is one interval from now
        if (!on) return o;                                   // the worker stops repeating by itself
    }
    if (!on && !hooked()) return o;
    enqueue_console(*c, console_lines(*c, on ? CheatAction::On : CheatAction::Off));
    return o;
}

Trainer::Outcome Trainer::set_number(const std::string& id, double value, const std::string& mode) {
    std::lock_guard l(mu_);
    const CheatDef* c = def_.find_cheat(id);
    if (!c || c->kind != CheatKind::Number) { Outcome o; o.error = "unknown number cheat \"" + id + "\""; return o; }
    if (!c->available()) { Outcome o; o.error = c->unsupported.empty() ? "Not available" : c->unsupported; return o; }
    value = std::clamp(value, c->min, c->max);
    if (auto why = gate(*c)) return deny(*c, *why);
    if (c->memory) {
        std::string err;
        double v = value;
        if (mode == "add") {
            auto cur = read_memory(*c->memory, &err);
            if (!cur) return deny(*c, err);
            v += *cur;
        }
        if (!write_memory(*c->memory, v, &err)) return deny(*c, err);
        log_info(c->label + " = " + format_number(v));
        Outcome o;
        o.ok = true;
        o.value = v;
        return o;
    }
    auto lines = console_lines(*c, CheatAction::Number, value, mode);
    if (lines.empty()) return deny(*c, "this cheat has no command for the \"" + mode + "\" mode");
    enqueue_console(*c, std::move(lines));
    Outcome o;
    o.ok = true;
    o.value = value;
    return o;
}

Trainer::Outcome Trainer::press(const std::string& id) {
    std::lock_guard l(mu_);
    const CheatDef* c = def_.find_cheat(id);
    if (!c || c->kind != CheatKind::Button) { Outcome o; o.error = "unknown button \"" + id + "\""; return o; }
    if (auto why = gate(*c)) return deny(*c, *why);
    if (!c->launch) enqueue_console(*c, console_lines(*c, CheatAction::Press));
    Outcome o;
    o.ok = true;
    return o;
}

void Trainer::set_option(const std::string& id, const std::string& option, double value) {
    std::lock_guard l(mu_);
    opts_[id][option] = value;
}

bool Trainer::toggle_on(const std::string& id) const {
    std::lock_guard l(mu_);
    auto it = toggles_.find(id);
    return it != toggles_.end() && it->second;
}

std::map<std::string, bool> Trainer::toggles() const {
    std::lock_guard l(mu_);
    return toggles_;
}

// ------------------------------------------------------------------ workers
void Trainer::worker() {
    int64_t last_alive_check = 0;
    while (!stop_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        int64_t now = now_ms();
        std::shared_ptr<Process> p;
        {
            std::lock_guard l(mu_);
            p = proc_;
        }
        if (!p) continue;
        if (now - last_alive_check > 500) {
            last_alive_check = now;
            if (!p->alive()) { handle_exit(); continue; }
        }
        std::lock_guard l(mu_);
        if (!proc_) continue;
        for (auto& c : def_.cheats) {
            if (c.kind != CheatKind::Toggle) continue;
            auto t = toggles_.find(c.id);
            if (t == toggles_.end() || !t->second) continue;
            if (c.memory && c.memory->lock > 0) {
                if (now - lock_last_[c.id] >= (int64_t)(c.memory->lock * 1000)) {
                    lock_last_[c.id] = now;
                    std::string err;
                    if (!write_memory(*c.memory, c.memory->on, &err)) {
                        toggles_[c.id] = false;
                        log_error(c.label + " stopped: " + err);
                    }
                }
            } else if (!c.memory && c.interval) {
                double every = c.interval.value();
                if (auto o = opts_.find(c.id); o != opts_.end())
                    if (auto v = o->second.find("interval"); v != o->second.end()) every = v->second;
                if (now - repeat_last_[c.id] >= (int64_t)(every * 1000) && !gate(c)) {
                    repeat_last_[c.id] = now;
                    enqueue_console(c, console_lines(c, CheatAction::On));
                }
            }
        }
    }
}

void Trainer::executor() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock l(q_mu_);
            q_cv_.wait(l, [&] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        task();
    }
}

}  // namespace hx
