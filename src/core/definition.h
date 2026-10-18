#pragma once
// Trainer definition files (JSON, one per game). The schema is the one the game catalog already uses.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/result.h"
#include "core/util.h"
#include "core/value.h"

namespace hx {

enum class CheatKind { Number, Toggle, Button };
enum class Verified { Wiki, Inferred, Tested, Parsed, Unsupported };

const char* verified_name(Verified v);
const char* kind_name(CheatKind k);

struct RipSpec {
    int disp_at = 0;     // offset of the int32 displacement inside the matched bytes
    int insn_len = 0;    // length of the instruction (RIP-relative targets are measured from its end)
};

// A named memory location, found by byte signature (patch resilient), fixed RVA for known builds, or a pointer chain.
struct SymbolSpec {
    std::string name;
    ValueType type = ValueType::U8;
    std::string aob;                        // "48 8B 0D ?? ?? ?? ?? ..."
    std::string section = ".text";          // section of the main module that is searched
    std::optional<RipSpec> rip;
    int64_t offset = 0;                     // added to the match when there is no rip
    std::optional<uint64_t> rva;            // fallback / fixed address: only trusted for exe_size or exe_sha256 matches
    std::optional<uint64_t> exe_size;
    std::vector<int64_t> deref;             // pointer chain applied each time the symbol is read or written
    std::optional<int> index;               // which match to use when the pattern is not unique
    bool unique = true;                     // refuse ambiguous patterns unless false
    std::string chain;                      // alternative: "module+rva,off1,off2"
};

struct MemorySpec {
    std::string symbol;                     // refers to GameDef::symbols
    std::string chain;                      // or an inline pointer chain
    std::optional<ValueType> type;          // overrides the symbol's type
    double scale = 1;                       // number cheats: written value = value * scale
    double on = 1, off = 0;
    bool has_on = false, has_off = false;
    double lock = 0;                        // toggles: re-assert `on` every `lock` seconds while enabled
    bool restore = false;                   // put back the value the game had instead of writing `off`
};

struct OptionSpec {
    std::string id, label;
    double min = 0, max = 1000, def = 0;
};

struct CheatDef {
    std::string id, label;
    CheatKind kind = CheatKind::Number;
    std::string section = "Player";
    std::string info;
    std::string set, add;                   // console templates with {value}
    std::vector<std::string> on, off, run;  // console lines
    std::optional<double> interval;         // toggles: repeat `on` every N seconds
    std::vector<OptionSpec> options;
    std::optional<MemorySpec> memory;
    double def = 0, min = -1e9, max = 1e9, step = 100;
    std::string requires_;                  // DLC / precondition shown in the UI
    Verified verified = Verified::Inferred;
    std::string unsupported;
    bool launch = false;                    // button that launches the game

    [[nodiscard]] bool available() const { return verified != Verified::Unsupported; }
    [[nodiscard]] bool is_memory() const { return memory.has_value(); }
};

struct ConsoleConfig {
    int toggle_scancode = 0x29;             // backtick / tilde key
    std::vector<std::string> toggle_combo;  // e.g. {"shift","alt","c"}; overrides the scancode
    double open_delay = 0.25, line_delay = 0.12, type_delay = 0.008;
    bool close_after = true;
    std::string requires_symbol;            // console cheats are refused unless this symbol reads non-zero
};

struct GameDef {
    std::string id, name, exe, window_title, launch_args, notes;
    std::optional<int64_t> steam_appid;
    nlohmann::json match = nlohmann::json::object();       // {"steam_appid": N} / {"epic": "..."} / {"gog": "..."}
    int version = 1;                        // informational; updates are detected by content hash
    std::string min_app;                    // oldest Hexwright that understands this file
    std::vector<std::string> exe_sha256;    // exe hashes this definition was written for (version check)
    ConsoleConfig console;
    std::vector<SymbolSpec> symbols;
    std::vector<CheatDef> cheats;

    [[nodiscard]] const CheatDef* find_cheat(const std::string& id) const;
    [[nodiscard]] const SymbolSpec* find_symbol(const std::string& name) const;
    [[nodiscard]] nlohmann::json match_spec() const;       // match, or {"steam_appid": ...}
};

struct Diagnostics {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    [[nodiscard]] bool ok() const { return errors.empty(); }
};

constexpr size_t kMaxDefinitionBytes = 256 * 1024;

// Strict parse of untrusted JSON: unknown keys, bad ids, oversize files and dangling symbol references are errors.
Result<GameDef> parse_game(const nlohmann::json& j);
Result<GameDef> parse_game_text(std::string_view text);
Result<GameDef> load_game_file(const fs::path& path);

// Thorough check for authors: every problem at once, with typo suggestions and warnings.
Diagnostics validate_game_text(std::string_view text);

// Console templates: "{value}" is replaced, "{{" / "}}" are literal braces. Returns nullopt on a stray brace.
std::optional<std::string> format_template(const std::string& tpl, const std::string& value);
std::string format_number(double v);                       // 5000 -> "5000", 1.5 -> "1.5"

enum class CheatAction { Number, On, Off, Press };
// The console lines a cheat sends for an action. mode: "set" or "add" (numbers).
std::vector<std::string> console_lines(const CheatDef& c, CheatAction action, double value = 0, const std::string& mode = "set");

// What each cheat would do, for `hexctl preview`.
std::vector<std::string> preview_game(const GameDef& g);

}  // namespace hx
