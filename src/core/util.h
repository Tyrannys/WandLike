#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hx {

namespace fs = std::filesystem;

// ---------------------------------------------------------------- strings
std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);
std::string lower(std::string_view s);
std::string trim(std::string_view s);
bool iequals(std::string_view a, std::string_view b);
bool starts_with_i(std::string_view s, std::string_view prefix);
std::vector<std::string> split(std::string_view s, char sep);
std::string hex_encode(const uint8_t* data, size_t n);
std::string format_hex(uint64_t v, int min_digits = 1);   // "7FF6FCAA0000" (upper case, no 0x)
std::optional<uint64_t> parse_hex(std::string_view s);    // accepts "0x1F", "1f"

// ------------------------------------------------------------------ files
std::optional<std::string> read_file(const fs::path& p, size_t max_bytes = 64u << 20);
bool write_file_atomic(const fs::path& p, std::string_view data);   // temp file + MoveFileEx
fs::path local_app_data();                                          // %LOCALAPPDATA%
fs::path roaming_app_data();                                        // %APPDATA%
fs::path exe_path();                                                // path of the running executable
fs::path data_dir();                                                // %APPDATA%\Hexwright (created on demand)
std::string env_var(const char* name);

// ------------------------------------------------------------------- time
int64_t now_ms();               // monotonic
int64_t unix_seconds();         // wall clock
std::string clock_string();     // "HH:MM:SS"

// ---------------------------------------------------------------- logging
enum class Level { Debug, Info, Warn, Error };

struct LogLine {
    int64_t id = 0;
    Level level = Level::Info;
    std::string time;
    std::string text;
};

void log(Level level, std::string_view text);
inline void log_info(std::string_view t) { log(Level::Info, t); }
inline void log_warn(std::string_view t) { log(Level::Warn, t); }
inline void log_error(std::string_view t) { log(Level::Error, t); }
inline void log_debug(std::string_view t) { log(Level::Debug, t); }
std::vector<LogLine> recent_log(size_t max_lines = 200);
void set_log_to_console(bool on);          // debug build: mirror every line to the console
void set_log_file(const fs::path& path);   // also append to a file (debug.log)
void set_log_listener(std::function<void(const LogLine&)> fn);   // called under no lock

}  // namespace hx
