#include "core/util.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>

namespace hx {

// ---------------------------------------------------------------- strings
std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(std::wstring_view w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string lower(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return r;
}

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return std::string(s.substr(a, b - a));
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

bool starts_with_i(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && iequals(s.substr(0, p.size()), p);
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t i = s.find(sep, start);
        if (i == std::string_view::npos) { out.emplace_back(s.substr(start)); break; }
        out.emplace_back(s.substr(start, i - start));
        start = i + 1;
    }
    return out;
}

std::string hex_encode(const uint8_t* d, size_t n) {
    static const char* k = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { s.push_back(k[d[i] >> 4]); s.push_back(k[d[i] & 15]); }
    return s;
}

std::string format_hex(uint64_t v, int min_digits) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%0*llX", min_digits, (unsigned long long)v);
    return buf;
}

std::optional<uint64_t> parse_hex(std::string_view s) {
    s = std::string_view(s.data(), s.size());
    std::string t = trim(s);
    if (t.size() > 1 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t = t.substr(2);
    if (t.empty() || t.size() > 16) return std::nullopt;
    uint64_t v = 0;
    for (char c : t) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return std::nullopt;
        v = (v << 4) | (uint64_t)d;
    }
    return v;
}

// ------------------------------------------------------------------ files
std::optional<std::string> read_file(const fs::path& p, size_t max_bytes) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(0, std::ios::end);
    auto size = (size_t)f.tellg();
    if (size > max_bytes) return std::nullopt;
    f.seekg(0);
    std::string s(size, '\0');
    f.read(s.data(), (std::streamsize)size);
    return s;
}

bool write_file_atomic(const fs::path& p, std::string_view data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), (std::streamsize)data.size());
        if (!f) return false;
    }
    return MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

static fs::path known_folder(const KNOWNFOLDERID& id) {
    PWSTR w = nullptr;
    fs::path p;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &w))) p = w;
    CoTaskMemFree(w);
    return p;
}

fs::path local_app_data() {
    if (auto v = env_var("HX_LOCALAPPDATA"); !v.empty()) return widen(v);   // tests / portable override
    return known_folder(FOLDERID_LocalAppData);
}

fs::path roaming_app_data() {
    if (auto v = env_var("HX_APPDATA"); !v.empty()) return widen(v);
    return known_folder(FOLDERID_RoamingAppData);
}

fs::path exe_path() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    return fs::path(std::wstring(buf, n));
}

fs::path data_dir() {
    fs::path p = roaming_app_data() / "Hexwright";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

std::string env_var(const char* name) {
    char* v = nullptr;
    size_t n = 0;
    if (_dupenv_s(&v, &n, name) != 0 || !v) return {};
    std::string s(v);
    free(v);
    return s;
}

// ------------------------------------------------------------------- time
int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int64_t unix_seconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string clock_string() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// ---------------------------------------------------------------- logging
namespace {
std::mutex g_log_mu;
std::deque<LogLine> g_log;
std::atomic<int64_t> g_log_id{0};
bool g_console = false;
fs::path g_log_file;
std::function<void(const LogLine&)> g_listener;
}  // namespace

void log(Level level, std::string_view text) {
    LogLine line{++g_log_id, level, clock_string(), std::string(text)};
    std::function<void(const LogLine&)> listener;
    {
        std::lock_guard lock(g_log_mu);
        g_log.push_back(line);
        if (g_log.size() > 500) g_log.pop_front();
        listener = g_listener;
        const char* tag = level == Level::Error ? "ERROR" : level == Level::Warn ? "WARN " : level == Level::Debug ? "DEBUG" : "INFO ";
        if (g_console) std::fprintf(stdout, "%s %s  %s\n", line.time.c_str(), tag, line.text.c_str()), std::fflush(stdout);
        if (!g_log_file.empty()) {
            std::ofstream f(g_log_file, std::ios::app);
            f << line.time << ' ' << tag << "  " << line.text << '\n';
        }
    }
    if (listener) listener(line);
}

std::vector<LogLine> recent_log(size_t max_lines) {
    std::lock_guard lock(g_log_mu);
    size_t n = std::min(max_lines, g_log.size());
    return std::vector<LogLine>(g_log.end() - (std::ptrdiff_t)n, g_log.end());
}

void set_log_to_console(bool on) { std::lock_guard l(g_log_mu); g_console = on; }
void set_log_file(const fs::path& p) { std::lock_guard l(g_log_mu); g_log_file = p; }
void set_log_listener(std::function<void(const LogLine&)> fn) { std::lock_guard l(g_log_mu); g_listener = std::move(fn); }

}  // namespace hx
