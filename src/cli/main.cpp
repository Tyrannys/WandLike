// hexctl: command-line companion to Hexwright (definition authoring, catalog index, process / scanner helpers).
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "core/aob.h"
#include "core/authoring.h"
#include "core/definition.h"
#include "core/library.h"
#include "core/pointer.h"
#include "core/safety.h"
#include "core/scanner.h"
#include "core/session.h"
#include "core/util.h"
#include "hx/version.h"

using namespace hx;

namespace {

int usage() {
    std::puts(
        "hexctl " HX_VERSION_STRING " - Hexwright command-line tool\n"
        "\n"
        "Definitions\n"
        "  hexctl validate FILE.json            check a game definition (every problem at once, with typo suggestions)\n"
        "  hexctl preview FILE.json             show exactly what each cheat would do (no game needed)\n"
        "  hexctl find-games [TEXT]             list games found in your Steam / Epic / GOG libraries\n"
        "  hexctl new-game TEXT [--out DIR]     write a starter definition for a detected game\n"
        "  hexctl catalog [--check] [--dir DIR] regenerate (or verify) wandlike/games/_index.json\n"
        "\n"
        "Processes and memory (single-player games only; anti-cheat / multiplayer targets are refused)\n"
        "  hexctl procs [TEXT]                  list running processes\n"
        "  hexctl check PID|NAME                run the safety check for a process\n"
        "  hexctl sections PID|NAME             PE sections of the main module\n"
        "  hexctl aob PID|NAME \"48 8B ?? 05\" [--section .text]   signature scan\n"
        "  hexctl read PID|NAME ADDR|CHAIN TYPE read a value (CHAIN like game.exe+1A2B,10,8)\n"
        "  hexctl scan PID|NAME TYPE VALUE      first scan for an exact value, prints the first hits\n"
        "      TYPE: byte int16 int32 int64 float double\n");
    return 2;
}

std::string read_text(const char* path, bool* ok) {
    auto t = read_file(widen(path), kMaxDefinitionBytes * 4);
    *ok = t.has_value();
    return t.value_or("");
}

std::optional<uint32_t> resolve_target(const std::string& arg) {
    if (!arg.empty() && std::all_of(arg.begin(), arg.end(), [](unsigned char c) { return std::isdigit(c); })) return (uint32_t)std::stoul(arg);
    auto pids = find_pids(arg);
    if (pids.empty()) return std::nullopt;
    return pids.front();
}

// Attach through the Session so the safety rules apply to every command that touches a process.
bool attach(Session& s, const std::string& arg) {
    auto pid = resolve_target(arg);
    if (!pid) { std::fprintf(stderr, "no running process matches \"%s\"\n", arg.c_str()); return false; }
    auto r = s.attach(*pid);
    if (!r.ok()) { std::fprintf(stderr, "refused: %s\n", r.message().c_str()); return false; }
    return true;
}

int cmd_validate(int argc, char** argv, bool preview) {
    if (argc < 3) return usage();
    bool ok;
    std::string text = read_text(argv[2], &ok);
    if (!ok) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
    Diagnostics d = validate_game_text(text);
    if (preview) {
        if (!d.ok()) {
            std::puts("fix these first (hexctl validate FILE):");
            for (auto& e : d.errors) std::printf("  %s\n", e.c_str());
            return 1;
        }
        auto g = parse_game_text(text);
        for (auto& l : preview_game(*g)) std::puts(l.c_str());
        return 0;
    }
    for (auto& w : d.warnings) std::printf("warning: %s\n", w.c_str());
    for (auto& e : d.errors) std::printf("ERROR:   %s\n", e.c_str());
    if (d.ok() && d.warnings.empty()) std::puts("OK: definition is valid");
    else std::printf("%zu error(s), %zu warning(s)\n", d.errors.size(), d.warnings.size());
    return d.ok() ? 0 : 1;
}

int cmd_find_games(int argc, char** argv) {
    std::string q = argc > 2 ? lower(argv[2]) : "";
    auto lib = Library::with_defaults();
    int n = 0;
    for (auto& g : lib.scan()) {
        if (!q.empty() && lower(g.name).find(q) == std::string::npos) continue;
        std::printf("%-6s %10s  %s   %s\n", g.store.c_str(), g.id.c_str(), g.name.c_str(), narrow(g.install_dir.wstring()).c_str());
        ++n;
    }
    std::printf("%d game(s)\n", n);
    return 0;
}

int cmd_new_game(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string q = lower(argv[2]);
    fs::path out = fs::path(widen("wandlike")) / "games";
    for (int i = 3; i + 1 < argc; ++i) if (std::string(argv[i]) == "--out") out = widen(argv[i + 1]);
    auto lib = Library::with_defaults();
    std::vector<LibraryGame> hits;
    for (auto& g : lib.scan()) if (lower(g.name).find(q) != std::string::npos) hits.push_back(g);
    if (hits.size() != 1) {
        std::printf("%zu detected games match \"%s\"%s\n", hits.size(), argv[2], hits.empty() ? ". Try `hexctl find-games` to see what was detected." : ":");
        for (size_t i = 0; i < hits.size() && i < 20; ++i) std::printf("  %s  (%s %s)\n", hits[i].name.c_str(), hits[i].store.c_str(), hits[i].id.c_str());
        return 2;
    }
    nlohmann::json def = scaffold_definition(hits[0]);
    fs::path file = out / widen(def["id"].get<std::string>() + ".json");
    if (fs::exists(file)) { std::printf("%s already exists; not overwriting\n", narrow(file.wstring()).c_str()); return 2; }
    if (!write_file_atomic(file, def.dump(2) + "\n")) { std::fprintf(stderr, "cannot write %s\n", narrow(file.wstring()).c_str()); return 1; }
    std::printf("wrote %s\nnext: edit the cheats, then run `hexctl validate` and `hexctl preview` on it\n", narrow(file.wstring()).c_str());
    return 0;
}

int cmd_catalog(int argc, char** argv) {
    bool check = false;
    fs::path dir = fs::path(widen("wandlike")) / "games";
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--check") check = true;
        else if (std::string(argv[i]) == "--dir" && i + 1 < argc) dir = widen(argv[++i]);
    }
    std::string err;
    std::string text = build_index_text(dir, &err);
    if (text.empty()) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    fs::path index = dir / "_index.json";
    if (check) {
        std::string current = read_file(index).value_or("");
        current.erase(std::remove(current.begin(), current.end(), '\r'), current.end());
        if (current != text) { std::puts("_index.json is stale: run `hexctl catalog`"); return 1; }
        std::puts("_index.json is up to date");
        return 0;
    }
    if (!write_file_atomic(index, text)) { std::fprintf(stderr, "cannot write %s\n", narrow(index.wstring()).c_str()); return 1; }
    std::printf("wrote %s\n", narrow(index.wstring()).c_str());
    return 0;
}

int cmd_procs(int argc, char** argv) {
    std::string q = argc > 2 ? lower(argv[2]) : "";
    auto procs = list_processes();
    std::sort(procs.begin(), procs.end(), [](const ProcessInfo& a, const ProcessInfo& b) { return lower(a.name) < lower(b.name); });
    int n = 0;
    for (auto& p : procs) {
        if (!q.empty() && lower(p.name).find(q) == std::string::npos) continue;
        std::printf("%7u  %s\n", p.pid, p.name.c_str());
        ++n;
    }
    std::printf("%d process(es)\n", n);
    return 0;
}

int cmd_check(int argc, char** argv) {
    if (argc < 3) return usage();
    auto pid = resolve_target(argv[2]);
    if (!pid) { std::fprintf(stderr, "no running process matches \"%s\"\n", argv[2]); return 1; }
    std::string name;
    for (auto& p : list_processes()) if (p.pid == *pid) name = p.name;
    auto v = check_attach(*pid, name);
    if (v.allowed) { std::printf("%s (pid %u): allowed\n", name.c_str(), *pid); return 0; }
    std::printf("%s (pid %u): REFUSED\n  %s\n", name.c_str(), *pid, v.reason.c_str());
    return 3;
}

int cmd_sections(int argc, char** argv) {
    if (argc < 3) return usage();
    Session s;
    if (!attach(s, argv[2])) return 3;
    auto m = s.process()->main_module();
    if (!m) { std::fputs("cannot find the main module\n", stderr); return 1; }
    std::printf("%s %s base %s size %llu  sha256 %s\n", m->name.c_str(), s.info()->is64 ? "x64" : "x86", format_hex(m->base).c_str(),
                (unsigned long long)m->size, s.info()->exe_sha256.c_str());
    auto secs = s.process()->sections(m->base);
    if (!secs.ok()) { std::fprintf(stderr, "%s\n", secs.message().c_str()); return 1; }
    for (auto& sec : *secs)
        std::printf("  %-8s rva %8s  size %10llu  %s%s\n", sec.name.c_str(), format_hex(sec.rva).c_str(), (unsigned long long)sec.size,
                    sec.executable ? "X" : "-", sec.writable ? "W" : "-");
    return 0;
}

int cmd_aob(int argc, char** argv) {
    if (argc < 4) return usage();
    std::string section = ".text";
    for (int i = 4; i + 1 < argc; ++i) if (std::string(argv[i]) == "--section") section = argv[i + 1];
    auto pat = Pattern::parse(argv[3]);
    if (!pat.ok()) { std::fprintf(stderr, "bad pattern: %s\n", pat.message().c_str()); return 1; }
    Session s;
    if (!attach(s, argv[2])) return 3;
    auto m = s.process()->main_module();
    auto hits = scan_module(*s.process(), *m, section, *pat, 32);
    if (!hits.ok()) { std::fprintf(stderr, "%s\n", hits.message().c_str()); return 1; }
    for (uint64_t a : *hits) std::printf("%s  (%s+%s)\n", format_hex(a).c_str(), m->name.c_str(), format_hex(a - m->base).c_str());
    std::printf("%zu match(es)%s\n", hits->size(), hits->size() == 32 ? " (stopped at 32)" : "");
    return 0;
}

int cmd_read(int argc, char** argv) {
    if (argc < 5) return usage();
    auto type = parse_type(argv[4]);
    if (!type) { std::fprintf(stderr, "unknown type \"%s\"\n", argv[4]); return 1; }
    Session s;
    if (!attach(s, argv[2])) return 3;
    auto chain = PointerChain::parse(argv[3]);
    if (!chain.ok()) { std::fprintf(stderr, "%s\n", chain.message().c_str()); return 1; }
    auto addr = resolve_chain(*s.process(), *chain);
    if (!addr.ok()) { std::fprintf(stderr, "%s\n", addr.message().c_str()); return 1; }
    uint8_t buf[8] = {};
    if (!s.process()->read(*addr, buf, type_size(*type))) { std::fprintf(stderr, "cannot read %s\n", format_hex(*addr).c_str()); return 1; }
    std::printf("%s = %s\n", format_hex(*addr).c_str(), Value::from_bytes(*type, buf).format().c_str());
    return 0;
}

int cmd_scan(int argc, char** argv) {
    if (argc < 5) return usage();
    auto type = parse_type(argv[3]);
    if (!type) { std::fprintf(stderr, "unknown type \"%s\"\n", argv[3]); return 1; }
    auto v = Value::parse(*type, argv[4]);
    if (!v) { std::fprintf(stderr, "\"%s\" is not a valid %s\n", argv[4], argv[3]); return 1; }
    Session s;
    if (!attach(s, argv[2])) return 3;
    Scanner sc;
    ScanConfig cfg;
    cfg.type = *type;
    ScanProgress prog;
    auto r = sc.first_scan(*s.process(), cfg, {ScanOp::Exact, *v, {}}, &prog);
    if (!r.ok()) { std::fprintf(stderr, "%s\n", r.message().c_str()); return 1; }
    std::printf("%llu result(s)\n", (unsigned long long)*r);
    for (size_t i = 0; i < sc.hits().size() && i < 20; ++i)
        std::printf("  %s\n", format_hex(sc.hits()[i].address).c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    std::string c = argv[1];
    if (c == "--version" || c == "version") { std::puts(HX_VERSION_STRING); return 0; }
    if (c == "validate") return cmd_validate(argc, argv, false);
    if (c == "preview") return cmd_validate(argc, argv, true);
    if (c == "find-games") return cmd_find_games(argc, argv);
    if (c == "new-game") return cmd_new_game(argc, argv);
    if (c == "catalog") return cmd_catalog(argc, argv);
    if (c == "procs") return cmd_procs(argc, argv);
    if (c == "check") return cmd_check(argc, argv);
    if (c == "sections") return cmd_sections(argc, argv);
    if (c == "aob") return cmd_aob(argc, argv);
    if (c == "read") return cmd_read(argc, argv);
    if (c == "scan") return cmd_scan(argc, argv);
    return usage();
}
