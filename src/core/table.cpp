#include "core/table.h"

#include <chrono>

#include <nlohmann/json.hpp>

#include "core/util.h"

namespace hx {

using nlohmann::json;

size_t AddressTable::size() const { std::lock_guard l(mu_); return entries_.size(); }
std::vector<TableEntry> AddressTable::snapshot() const { std::lock_guard l(mu_); return entries_; }

size_t AddressTable::add(TableEntry e) {
    std::lock_guard l(mu_);
    entries_.push_back(std::move(e));
    return entries_.size() - 1;
}

void AddressTable::remove(size_t i) {
    std::lock_guard l(mu_);
    if (i < entries_.size()) entries_.erase(entries_.begin() + (std::ptrdiff_t)i);
}

void AddressTable::clear() { std::lock_guard l(mu_); entries_.clear(); }

void AddressTable::rename(size_t i, std::string label) {
    std::lock_guard l(mu_);
    if (i < entries_.size()) entries_[i].label = std::move(label);
}

void AddressTable::set_type(size_t i, ValueType t) {
    std::lock_guard l(mu_);
    if (i >= entries_.size()) return;
    auto& e = entries_[i];
    e.freeze_value = Value::from_double(t, e.freeze_value.as_double());
    e.type = t;
}

std::optional<Value> AddressTable::read(const Process& p, size_t i, std::string* error) const {
    TableEntry e;
    {
        std::lock_guard l(mu_);
        if (i >= entries_.size()) { if (error) *error = "no such entry"; return std::nullopt; }
        e = entries_[i];
    }
    auto addr = resolve_chain(p, e.address);
    if (!addr.ok()) { if (error) *error = addr.message(); return std::nullopt; }
    uint8_t buf[8] = {};
    if (!p.read(*addr, buf, type_size(e.type))) { if (error) *error = "could not read " + format_hex(*addr); return std::nullopt; }
    return Value::from_bytes(e.type, buf);
}

bool AddressTable::write(const Process& p, size_t i, const Value& v, std::string* error) {
    TableEntry e;
    {
        std::lock_guard l(mu_);
        if (i >= entries_.size()) { if (error) *error = "no such entry"; return false; }
        e = entries_[i];
    }
    auto addr = resolve_chain(p, e.address);
    if (!addr.ok()) { if (error) *error = addr.message(); return false; }
    Value typed = v.type == e.type ? v : Value::from_double(e.type, v.as_double());
    uint8_t buf[8] = {};
    typed.to_bytes(buf);
    if (!p.write(*addr, buf, type_size(e.type))) { if (error) *error = "could not write " + format_hex(*addr); return false; }
    return true;
}

bool AddressTable::set_frozen(size_t i, bool frozen, std::optional<Value> value) {
    std::lock_guard l(mu_);
    if (i >= entries_.size()) return false;
    entries_[i].frozen = frozen;
    if (value) entries_[i].freeze_value = value->type == entries_[i].type ? *value : Value::from_double(entries_[i].type, value->as_double());
    return true;
}

size_t AddressTable::apply_frozen(const Process& p) {
    std::vector<TableEntry> frozen;
    {
        std::lock_guard l(mu_);
        for (auto& e : entries_) if (e.frozen) frozen.push_back(e);
    }
    size_t n = 0;
    for (auto& e : frozen) {
        auto addr = resolve_chain(p, e.address);
        if (!addr.ok()) continue;
        uint8_t buf[8] = {};
        e.freeze_value.to_bytes(buf);
        if (p.write(*addr, buf, type_size(e.type))) ++n;
    }
    return n;
}

// -------------------------------------------------------------- persistence
std::string AddressTable::to_json(const std::string& exe_name, const std::string& exe_sha256) const {
    json j;
    j["version"] = 1;
    j["exe"] = exe_name;
    j["exe_sha256"] = exe_sha256;
    j["entries"] = json::array();
    for (auto& e : snapshot()) {
        json o;
        o["label"] = e.label;
        o["type"] = type_name(e.type);
        o["address"] = e.address.format();
        o["frozen"] = e.frozen;
        o["value"] = e.freeze_value.format();
        if (!e.note.empty()) o["note"] = e.note;
        j["entries"].push_back(std::move(o));
    }
    return j.dump(2);
}

std::string AddressTable::load_json(const std::string& text, const std::string& current_sha, std::string* build_warning) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("entries") || !j["entries"].is_array()) return "not a Hexwright table file";
    std::vector<TableEntry> loaded;
    for (auto& o : j["entries"]) {
        if (!o.is_object()) return "bad entry in table file";
        TableEntry e;
        e.label = o.value("label", "");
        auto t = parse_type(o.value("type", "int32"));
        if (!t) return "unknown value type \"" + o.value("type", "") + "\"";
        e.type = *t;
        auto chain = PointerChain::parse(o.value("address", ""));
        if (!chain.ok()) return "bad address \"" + o.value("address", "") + "\": " + chain.message();
        e.address = *chain;
        e.frozen = false;                               // never start freezing on load: the user re-enables it
        e.freeze_value = Value::parse(e.type, o.value("value", "0")).value_or(Value::from_i64(e.type, 0));
        e.note = o.value("note", "");
        loaded.push_back(std::move(e));
    }
    if (build_warning) {
        std::string saved = lower(j.value("exe_sha256", ""));
        build_warning->clear();
        if (!saved.empty() && !current_sha.empty() && saved != lower(current_sha))
            *build_warning = "This table was saved for a different build of the game. Absolute addresses may be wrong; pointer chains usually still work.";
    }
    std::lock_guard l(mu_);
    entries_ = std::move(loaded);
    return "";
}

bool AddressTable::save(const fs::path& path, const std::string& exe_name, const std::string& exe_sha256) const {
    return write_file_atomic(path, to_json(exe_name, exe_sha256));
}

std::string AddressTable::load(const fs::path& path, const std::string& current_sha, std::string* build_warning) {
    auto text = read_file(path, 8u << 20);
    if (!text) return "cannot read " + narrow(path.wstring());
    return load_json(*text, current_sha, build_warning);
}

fs::path AddressTable::default_path(const std::string& exe_name) {
    std::string safe;
    for (char c : lower(exe_name)) safe.push_back(std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' ? c : '_');
    return data_dir() / "tables" / (safe + ".json");
}

// ------------------------------------------------------------------ freeze
void FreezeEngine::start(std::shared_ptr<Process> proc, AddressTable* table, int interval_ms) {
    stop();
    stop_ = false;
    thread_ = std::thread([this, proc = std::move(proc), table, interval_ms] {
        while (!stop_) {
            if (!proc->alive()) break;
            table->apply_frozen(*proc);
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
    });
}

void FreezeEngine::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

}  // namespace hx
