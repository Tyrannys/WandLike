#include "core/definition.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <regex>
#include <set>

#include "core/aob.h"
#include "core/pointer.h"

namespace hx {

using nlohmann::json;

const char* verified_name(Verified v) {
    switch (v) {
        case Verified::Wiki: return "wiki";
        case Verified::Inferred: return "inferred";
        case Verified::Tested: return "tested";
        case Verified::Parsed: return "parsed";
        default: return "unsupported";
    }
}

const char* kind_name(CheatKind k) {
    switch (k) {
        case CheatKind::Number: return "number";
        case CheatKind::Toggle: return "toggle";
        default: return "button";
    }
}

const CheatDef* GameDef::find_cheat(const std::string& cheat_id) const {
    for (auto& c : cheats) if (c.id == cheat_id) return &c;
    return nullptr;
}

const SymbolSpec* GameDef::find_symbol(const std::string& symbol_name) const {
    for (auto& s : symbols) if (s.name == symbol_name) return &s;
    return nullptr;
}

json GameDef::match_spec() const {
    if (!match.empty()) return match;
    json j = json::object();
    if (steam_appid) j["steam_appid"] = *steam_appid;
    return j;
}

// ------------------------------------------------------------ templates
std::string format_number(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) {
        char b[32];
        std::snprintf(b, sizeof b, "%lld", (long long)v);
        return b;
    }
    char b[40];
    std::snprintf(b, sizeof b, "%.15g", v);
    return b;
}

std::optional<std::string> format_template(const std::string& tpl, const std::string& value) {
    std::string out;
    for (size_t i = 0; i < tpl.size(); ++i) {
        char c = tpl[i];
        if (c == '{') {
            if (i + 1 < tpl.size() && tpl[i + 1] == '{') { out.push_back('{'); ++i; continue; }
            if (tpl.compare(i, 7, "{value}") == 0) { out += value; i += 6; continue; }
            return std::nullopt;
        }
        if (c == '}') {
            if (i + 1 < tpl.size() && tpl[i + 1] == '}') { out.push_back('}'); ++i; continue; }
            return std::nullopt;
        }
        out.push_back(c);
    }
    return out;
}

std::vector<std::string> console_lines(const CheatDef& c, CheatAction action, double value, const std::string& mode) {
    switch (action) {
        case CheatAction::Number: {
            const std::string& tpl = (mode == "set" && !c.set.empty()) ? c.set : (!c.add.empty() ? c.add : c.set);
            if (tpl.empty()) return {};
            auto s = format_template(tpl, format_number(value));
            return s ? std::vector<std::string>{*s} : std::vector<std::string>{};
        }
        case CheatAction::On: return c.on;
        case CheatAction::Off: return c.off;
        default: return c.run;
    }
}

// ------------------------------------------------------------- parsing
namespace {

const std::regex kIdRe("^[a-z0-9][a-z0-9_-]{0,63}$");

int edit_distance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

struct Walker {
    Diagnostics& d;

    void err(const std::string& where, const std::string& what) { d.errors.push_back(where + ": " + what); }
    void warn(const std::string& where, const std::string& what) { d.warnings.push_back(where + ": " + what); }

    void check_keys(const json& o, const std::set<std::string>& allowed, const std::string& where) {
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (allowed.count(it.key())) continue;
            std::string best;
            int best_d = 3;
            for (auto& a : allowed) {
                int dist = edit_distance(it.key(), a);
                if (dist < best_d) { best_d = dist; best = a; }
            }
            err(where, "unknown key \"" + it.key() + "\"" + (best.empty() ? "" : " (did you mean \"" + best + "\"?)"));
        }
    }

    bool get_string(const json& o, const char* key, std::string& out, const std::string& where) {
        auto it = o.find(key);
        if (it == o.end()) return false;
        if (!it->is_string()) { err(where, std::string("\"") + key + "\" must be a string"); return false; }
        out = it->get<std::string>();
        return true;
    }
    bool get_number(const json& o, const char* key, double& out, const std::string& where) {
        auto it = o.find(key);
        if (it == o.end()) return false;
        if (!it->is_number()) { err(where, std::string("\"") + key + "\" must be a number"); return false; }
        out = it->get<double>();
        return true;
    }
    bool get_bool(const json& o, const char* key, bool& out, const std::string& where) {
        auto it = o.find(key);
        if (it == o.end()) return false;
        if (!it->is_boolean()) { err(where, std::string("\"") + key + "\" must be true or false"); return false; }
        out = it->get<bool>();
        return true;
    }
    bool get_strings(const json& o, const char* key, std::vector<std::string>& out, const std::string& where) {
        auto it = o.find(key);
        if (it == o.end()) return false;
        if (!it->is_array()) { err(where, std::string("\"") + key + "\" must be a list of strings"); return false; }
        out.clear();
        for (auto& e : *it) {
            if (!e.is_string()) { err(where, std::string("\"") + key + "\" must contain only strings"); return false; }
            out.push_back(e.get<std::string>());
        }
        return true;
    }
    // integers written as 12345 or "0x1A2B"
    std::optional<uint64_t> as_u64(const json& v) {
        if (v.is_number_unsigned()) return v.get<uint64_t>();
        if (v.is_number_integer() && v.get<int64_t>() >= 0) return (uint64_t)v.get<int64_t>();
        if (v.is_string()) return parse_hex(v.get<std::string>());
        return std::nullopt;
    }
    std::optional<int64_t> as_i64(const json& v) {
        if (v.is_number_integer()) return v.get<int64_t>();
        if (v.is_string()) {
            std::string s = trim(v.get<std::string>());
            bool neg = !s.empty() && s[0] == '-';
            auto h = parse_hex(neg ? s.substr(1) : s);
            if (h) return neg ? -(int64_t)*h : (int64_t)*h;
        }
        return std::nullopt;
    }

    void template_ok(const std::string& tpl, const std::string& where, const char* key) {
        if (tpl.find("{value}") == std::string::npos)
            err(where, std::string("\"") + key + "\" must contain {value} where the number goes: \"" + tpl + "\"");
        if (!format_template(tpl, "1"))
            err(where, std::string("\"") + key + "\" has a stray brace; write literal { } as {{ }} in set/add templates: \"" + tpl + "\"");
    }

    ValueType get_type(const json& o, const std::string& where, ValueType def, bool* given = nullptr) {
        std::string s;
        if (given) *given = false;
        if (!get_string(o, "type", s, where)) return def;
        if (given) *given = true;
        auto t = parse_type(s);
        if (!t) { err(where, "type must be one of byte, int16, int32, int64, float, double (got \"" + s + "\")"); return def; }
        return *t;
    }

    SymbolSpec symbol(const std::string& name, const json& o) {
        std::string w = "symbol " + name;
        SymbolSpec s;
        s.name = name;
        if (!o.is_object()) { err(w, "must be an object"); return s; }
        check_keys(o, {"type", "aob", "rip", "offset", "rva", "exe_size", "section", "index", "deref", "unique", "chain"}, w);
        s.type = get_type(o, w, ValueType::I32);
        get_string(o, "aob", s.aob, w);
        get_string(o, "section", s.section, w);
        get_string(o, "chain", s.chain, w);
        get_bool(o, "unique", s.unique, w);
        double idx = 0;
        if (get_number(o, "index", idx, w)) s.index = (int)idx;
        if (auto it = o.find("offset"); it != o.end()) { if (auto v = as_i64(*it)) s.offset = *v; else err(w, "offset must be an integer"); }
        if (auto it = o.find("rva"); it != o.end()) { if (auto v = as_u64(*it)) s.rva = *v; else err(w, "rva must be an integer or hex string"); }
        if (auto it = o.find("exe_size"); it != o.end()) { if (auto v = as_u64(*it)) s.exe_size = *v; else err(w, "exe_size must be an integer"); }
        if (auto it = o.find("deref"); it != o.end()) {
            if (!it->is_array()) err(w, "deref must be a list of offsets");
            else for (auto& e : *it) { if (auto v = as_i64(e)) s.deref.push_back(*v); else err(w, "deref offsets must be integers"); }
        }
        if (auto it = o.find("rip"); it != o.end()) {
            if (!it->is_object() || !it->contains("disp_at") || !it->contains("insn_len") || it->size() != 2) err(w, "rip needs exactly disp_at and insn_len");
            else {
                RipSpec r;
                r.disp_at = it->at("disp_at").is_number_integer() ? it->at("disp_at").get<int>() : -1;
                r.insn_len = it->at("insn_len").is_number_integer() ? it->at("insn_len").get<int>() : -1;
                if (r.disp_at < 0 || r.insn_len <= 0) err(w, "rip values must be positive integers");
                else s.rip = r;
            }
        }
        if (!s.aob.empty()) {
            auto p = Pattern::parse(s.aob);
            if (!p.ok()) err(w, "aob: " + p.message());
        } else if (s.chain.empty() && !s.rva) {
            err(w, "needs an \"aob\" pattern, a \"chain\", or an \"rva\" with \"exe_size\"");
        }
        if (!s.chain.empty()) {
            auto c = PointerChain::parse(s.chain);
            if (!c.ok()) err(w, "chain: " + c.message());
        }
        if (s.rva && !s.exe_size && s.aob.empty()) warn(w, "a bare rva is only trusted when the exe size matches; add \"exe_size\"");
        return s;
    }

    CheatDef cheat(size_t index, const json& o, const GameDef& g) {
        CheatDef c;
        std::string w = "cheat #" + std::to_string(index + 1);
        if (!o.is_object()) { err(w, "must be an object"); return c; }
        if (o.contains("id") && o["id"].is_string()) w += " (" + o["id"].get<std::string>() + ")";
        check_keys(o, {"id", "label", "kind", "section", "info", "set", "add", "on", "off", "run", "interval", "options", "memory", "default",
                       "min", "max", "step", "requires", "verified", "unsupported", "launch"}, w);
        for (const char* req : {"id", "label", "kind"})
            if (!o.contains(req)) err(w, std::string("missing required key \"") + req + "\"");
        get_string(o, "id", c.id, w);
        get_string(o, "label", c.label, w);
        get_string(o, "section", c.section, w);
        get_string(o, "info", c.info, w);
        get_string(o, "set", c.set, w);
        get_string(o, "add", c.add, w);
        get_string(o, "requires", c.requires_, w);
        get_string(o, "unsupported", c.unsupported, w);
        get_strings(o, "on", c.on, w);
        get_strings(o, "off", c.off, w);
        get_strings(o, "run", c.run, w);
        get_bool(o, "launch", c.launch, w);
        get_number(o, "default", c.def, w);
        get_number(o, "min", c.min, w);
        get_number(o, "max", c.max, w);
        get_number(o, "step", c.step, w);
        double iv;
        if (get_number(o, "interval", iv, w)) { if (iv <= 0) err(w, "interval must be positive"); else c.interval = iv; }

        std::string kind;
        if (get_string(o, "kind", kind, w)) {
            if (kind == "number") c.kind = CheatKind::Number;
            else if (kind == "toggle") c.kind = CheatKind::Toggle;
            else if (kind == "button") c.kind = CheatKind::Button;
            else err(w, "kind must be one of number, toggle, button");
        }
        std::string ver;
        if (get_string(o, "verified", ver, w)) {
            if (ver == "wiki") c.verified = Verified::Wiki;
            else if (ver == "inferred") c.verified = Verified::Inferred;
            else if (ver == "tested") c.verified = Verified::Tested;
            else if (ver == "parsed") c.verified = Verified::Parsed;
            else if (ver == "unsupported") c.verified = Verified::Unsupported;
            else err(w, "verified must be one of tested, parsed, wiki, inferred, unsupported");
        }
        if (!c.set.empty()) template_ok(c.set, w, "set");
        if (!c.add.empty()) template_ok(c.add, w, "add");
        for (auto* lines : {&c.on, &c.off, &c.run})
            for (auto& l : *lines)
                if (l.find("{{") != std::string::npos || l.find("}}") != std::string::npos)
                    warn(w, "on/off/run lines are sent as written; remove the doubled braces in \"" + l + "\"");

        if (auto it = o.find("options"); it != o.end()) {
            if (!it->is_array()) err(w, "options must be a list");
            else for (auto& oo : *it) {
                OptionSpec op;
                if (!oo.is_object()) { err(w, "each option must be an object"); continue; }
                check_keys(oo, {"id", "label", "type", "min", "max", "default"}, w + " option");
                get_string(oo, "id", op.id, w + " option");
                get_string(oo, "label", op.label, w + " option");
                get_number(oo, "min", op.min, w + " option");
                get_number(oo, "max", op.max, w + " option");
                get_number(oo, "default", op.def, w + " option");
                c.options.push_back(op);
            }
        }
        if (auto it = o.find("memory"); it != o.end()) c.memory = memory(*it, w, g);

        if (c.kind == CheatKind::Number && c.set.empty() && c.add.empty() && !c.memory) err(w, "a number cheat needs \"set\" and/or \"add\" (or \"memory\")");
        if (c.kind == CheatKind::Toggle && c.on.empty() && !c.memory && c.verified != Verified::Unsupported)
            err(w, "a toggle needs \"on\" (and usually \"off\") commands, or \"memory\"");
        if (c.kind == CheatKind::Button && c.run.empty() && !c.launch) err(w, "a button needs \"run\" commands or \"launch\": true");
        if (c.memory && c.kind == CheatKind::Toggle && !c.memory->has_on) err(w, "a memory toggle needs \"on\" (and \"off\") values");
        if (c.verified == Verified::Unsupported && c.unsupported.empty()) warn(w, "explain why in \"unsupported\" so users know what is missing");
        if (c.kind == CheatKind::Number && !(c.min <= c.def && c.def <= c.max)) err(w, "default " + format_number(c.def) + " is outside min/max");
        if (c.info.empty()) warn(w, "add an \"info\" sentence; it becomes the tooltip");
        return c;
    }

    MemorySpec memory(const json& o, const std::string& w, const GameDef& g) {
        MemorySpec m;
        std::string where = w + " memory";
        if (!o.is_object()) { err(where, "must be an object"); return m; }
        check_keys(o, {"symbol", "type", "on", "off", "lock", "restore", "scale", "module", "offsets", "chain"}, where);
        get_string(o, "symbol", m.symbol, where);
        get_string(o, "chain", m.chain, where);
        bool given = false;
        ValueType t = get_type(o, where, ValueType::I32, &given);
        if (given) m.type = t;
        get_number(o, "scale", m.scale, where);
        m.has_on = get_number(o, "on", m.on, where);
        m.has_off = get_number(o, "off", m.off, where);
        get_number(o, "lock", m.lock, where);
        get_bool(o, "restore", m.restore, where);
        if (o.contains("offsets")) {                        // legacy raw spec: {"module": "...", "offsets": [...]}
            std::string module;
            get_string(o, "module", module, where);
            std::string chain = module.empty() ? "" : module + "+";
            bool first = true;
            if (!o["offsets"].is_array() || o["offsets"].empty()) err(where, "offsets must be a non-empty list");
            else for (auto& e : o["offsets"]) {
                auto v = as_i64(e);
                if (!v) { err(where, "offsets must be integers"); break; }
                chain += (first ? "" : ",") + std::string(*v < 0 ? "-" : "") + format_hex((uint64_t)(*v < 0 ? -*v : *v));
                first = false;
            }
            m.chain = chain;
        }
        if (m.symbol.empty() && m.chain.empty()) err(where, "needs a \"symbol\" (or a \"chain\")");
        if (!m.symbol.empty() && !g.find_symbol(m.symbol)) err(where, "unknown hook symbol \"" + m.symbol + "\"");
        if (!m.chain.empty() && !PointerChain::parse(m.chain).ok()) err(where, "chain is not a valid pointer chain");
        if (m.lock < 0) err(where, "lock must not be negative");
        return m;
    }
};

GameDef build_game(const json& j, Diagnostics& d) {
    Walker w{d};
    GameDef g;
    if (!j.is_object()) { w.err("plugin", "the file must contain a JSON object"); return g; }
    w.check_keys(j, {"id", "name", "exe", "window_title", "launch_args", "notes", "steam_appid", "match", "version", "min_app", "exe_sha256",
                     "console", "hook", "cheats", "source"}, "plugin");
    for (const char* req : {"id", "name", "exe", "window_title", "cheats"})
        if (!j.contains(req)) w.err("plugin", std::string("missing required key \"") + req + "\"");
    w.get_string(j, "id", g.id, "plugin");
    w.get_string(j, "name", g.name, "plugin");
    w.get_string(j, "exe", g.exe, "plugin");
    w.get_string(j, "window_title", g.window_title, "plugin");
    w.get_string(j, "launch_args", g.launch_args, "plugin");
    w.get_string(j, "notes", g.notes, "plugin");
    w.get_string(j, "min_app", g.min_app, "plugin");
    w.get_strings(j, "exe_sha256", g.exe_sha256, "plugin");
    for (auto& h : g.exe_sha256) h = lower(h);
    if (auto it = j.find("steam_appid"); it != j.end()) {
        if (it->is_number_integer()) g.steam_appid = it->get<int64_t>();
        else w.err("plugin", "steam_appid must be a whole number");
    }
    if (auto it = j.find("version"); it != j.end()) {
        if (it->is_number_integer() && it->get<int>() >= 1) g.version = it->get<int>();
        else w.err("plugin", "version must be a whole number >= 1");
    }
    if (auto it = j.find("match"); it != j.end()) {
        if (!it->is_object()) w.err("plugin", "match must be an object");
        else {
            for (auto m = it->begin(); m != it->end(); ++m)
                if (m.key() != "steam_appid" && m.key() != "epic" && m.key() != "gog") w.err("plugin", "unknown match key \"" + m.key() + "\"");
            g.match = *it;
        }
    }
    if (!g.id.empty() && !std::regex_match(g.id, kIdRe)) w.err("plugin", "id \"" + g.id + "\" must be lowercase letters, digits, - or _ (max 64)");
    if (!g.exe.empty() && !iequals(g.exe.size() > 4 ? g.exe.substr(g.exe.size() - 4) : g.exe, ".exe")) w.warn("plugin", "exe should be the process name, e.g. \"game.exe\"");
    if (g.match.empty() && !g.steam_appid) w.warn("plugin", "no \"match\" (e.g. {\"steam_appid\": 123}); the game cannot be recognised in the library");
    if (!g.min_app.empty() && !std::regex_match(g.min_app, std::regex("^[0-9]+(\\.[0-9]+){1,3}$"))) w.err("plugin", "min_app must look like 1.3.0");

    if (auto it = j.find("console"); it != j.end()) {
        if (!it->is_object()) w.err("console", "must be an object");
        else {
            w.check_keys(*it, {"toggle_scancode", "toggle_combo", "open_delay", "line_delay", "type_delay", "close_after", "requires_symbol"}, "console");
            double v;
            if (w.get_number(*it, "toggle_scancode", v, "console")) g.console.toggle_scancode = (int)v;
            if (w.get_number(*it, "open_delay", v, "console")) g.console.open_delay = v;
            if (w.get_number(*it, "line_delay", v, "console")) g.console.line_delay = v;
            if (w.get_number(*it, "type_delay", v, "console")) g.console.type_delay = v;
            w.get_bool(*it, "close_after", g.console.close_after, "console");
            w.get_strings(*it, "toggle_combo", g.console.toggle_combo, "console");
            w.get_string(*it, "requires_symbol", g.console.requires_symbol, "console");
        }
    }
    if (auto it = j.find("hook"); it != j.end()) {
        if (!it->is_object()) w.err("hook", "must be an object");
        else {
            w.check_keys(*it, {"symbols"}, "hook");
            if (auto s = it->find("symbols"); s != it->end()) {
                if (!s->is_object()) w.err("hook", "symbols must be an object");
                else for (auto sym = s->begin(); sym != s->end(); ++sym) {
                    if (!std::regex_match(sym.key(), std::regex("^[A-Za-z0-9_-]{1,64}$"))) w.err("hook", "bad symbol name \"" + sym.key() + "\"");
                    g.symbols.push_back(w.symbol(sym.key(), sym.value()));
                }
            }
        }
    }
    if (!g.console.requires_symbol.empty() && !g.find_symbol(g.console.requires_symbol))
        w.err("console", "requires_symbol \"" + g.console.requires_symbol + "\" is not defined under hook.symbols");

    if (auto it = j.find("cheats"); it != j.end()) {
        if (!it->is_array() || it->empty()) w.err("plugin", "\"cheats\" must be a non-empty list");
        else if (it->size() > 300) w.err("plugin", "at most 300 cheats per file");
        else {
            std::set<std::string> seen;
            for (size_t i = 0; i < it->size(); ++i) {
                CheatDef c = w.cheat(i, (*it)[i], g);
                if (!c.id.empty() && !seen.insert(c.id).second) w.err("cheat #" + std::to_string(i + 1) + " (" + c.id + ")", "duplicate id");
                g.cheats.push_back(std::move(c));
            }
        }
    }
    return g;
}

}  // namespace

Result<GameDef> parse_game(const json& j) {
    Diagnostics d;
    GameDef g = build_game(j, d);
    if (!d.ok()) return Error{d.errors.front()};
    return g;
}

Result<GameDef> parse_game_text(std::string_view text) {
    if (text.size() > kMaxDefinitionBytes) return Error{"file is larger than 256 KB"};
    json j = json::parse(text.begin(), text.end(), nullptr, false);
    if (j.is_discarded()) return Error{"not valid JSON"};
    return parse_game(j);
}

Result<GameDef> load_game_file(const fs::path& path) {
    auto text = read_file(path, kMaxDefinitionBytes + 1);
    if (!text) return Error{"cannot read " + narrow(path.wstring())};
    return parse_game_text(*text);
}

Diagnostics validate_game_text(std::string_view text) {
    Diagnostics d;
    if (text.size() > kMaxDefinitionBytes) { d.errors.push_back("file is larger than 256 KB"); return d; }
    try {
        json j = json::parse(text.begin(), text.end());
        build_game(j, d);
    } catch (const json::parse_error& e) {
        d.errors.push_back(std::string("not valid JSON: ") + e.what());
    }
    return d;
}

std::vector<std::string> preview_game(const GameDef& g) {
    std::vector<std::string> out;
    out.push_back(g.name + "  (definition v" + std::to_string(g.version) + ", match " + g.match_spec().dump() + ", exe " + g.exe + ")");
    out.emplace_back();
    auto join = [](const std::vector<std::string>& v) {
        if (v.empty()) return std::string("(nothing)");
        std::string s;
        for (size_t i = 0; i < v.size(); ++i) s += (i ? "  |  " : "") + v[i];
        return s;
    };
    for (auto& c : g.cheats) {
        std::string head = c.label + "  [" + verified_name(c.verified) + "]  ";
        if (c.memory) {
            std::string target = !c.memory->symbol.empty() ? "symbol " + c.memory->symbol : "chain " + c.memory->chain;
            out.push_back(head + "memory " + target + (c.kind == CheatKind::Toggle ? " on=" + format_number(c.memory->on) + " off=" + format_number(c.memory->off) : "") +
                          (c.memory->lock > 0 ? " lock=" + format_number(c.memory->lock) + "s" : "") + (c.memory->restore ? " restore" : ""));
        } else if (c.kind == CheatKind::Number) {
            for (const char* mode : {"set", "add"}) {
                if ((std::string(mode) == "set" && !c.set.empty()) || (std::string(mode) == "add" && !c.add.empty())) {
                    auto l = console_lines(c, CheatAction::Number, c.def, mode);
                    out.push_back(head + mode + " " + format_number(c.def) + " -> " + (l.empty() ? "(nothing)" : l[0]));
                }
            }
        } else if (c.kind == CheatKind::Toggle) {
            out.push_back(head + "on  -> " + join(c.on));
            out.push_back(std::string(head.size(), ' ') + "off -> " + join(c.off) + (c.interval ? "   repeats every " + format_number(*c.interval) + "s" : ""));
        } else {
            out.push_back(head + "run -> " + (c.run.empty() ? (c.launch ? "launch game" : "(nothing)") : join(c.run)));
        }
    }
    return out;
}

}  // namespace hx
