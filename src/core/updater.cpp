#include "core/updater.h"

#include <windows.h>

#include <nlohmann/json.hpp>

#include "core/catalog.h"
#include "core/hash.h"
#include "hx/version.h"

namespace hx {

using nlohmann::json;

UpdaterConfig default_updater_config() {
    UpdaterConfig c;
    c.repo = HX_UPDATE_REPO;
    c.current_version = HX_VERSION_STRING;
    c.asset_name = HX_SETUP_ASSET;
    c.fetch = [](const std::string& url, const HttpOptions& o) { return http_get(url, o); };
    c.download = [](const std::string& url, const fs::path& dest, const ProgressFn& p, const HttpOptions& o) { return http_download(url, dest, p, o); };
    return c;
}

std::optional<UpdateInfo> check_for_update(const UpdaterConfig& cfg, std::string* error) {
    auto fail = [&](const std::string& why) -> std::optional<UpdateInfo> { if (error) *error = why; return std::nullopt; };
    if (cfg.repo.empty()) return fail("updates are not configured");
    HttpOptions opt;
    opt.max_bytes = 2u << 20;
    opt.headers["Accept"] = "application/vnd.github+json";
    HttpResponse r = cfg.fetch("https://api.github.com/repos/" + cfg.repo + "/releases/latest", opt);
    if (r.status == 404) return fail("no releases found (is the repository public?)");
    if (r.status != 200) return fail(r.status == 0 ? (r.error.empty() ? "network error" : r.error) : "GitHub returned HTTP " + std::to_string(r.status));
    json j = json::parse(r.body, nullptr, false);
    if (!j.is_object()) return fail("unexpected response from GitHub");
    if (j.value("draft", false) || j.value("prerelease", false)) return std::nullopt;
    std::string tag = j.value("tag_name", "");
    if (tag.empty() || compare_versions(tag, cfg.current_version) <= 0) return std::nullopt;
    if (!j.contains("assets") || !j["assets"].is_array()) return std::nullopt;
    for (auto& a : j["assets"]) {
        if (a.value("name", "") != cfg.asset_name) continue;
        std::string url = a.value("browser_download_url", "");
        if (url.rfind("https://github.com/" + cfg.repo + "/releases/download/", 0) != 0) return fail("the release points at an unexpected URL");
        UpdateInfo u;
        u.version = tag[0] == 'v' || tag[0] == 'V' ? tag.substr(1) : tag;
        u.url = url;
        std::string digest = a.value("digest", "");
        if (digest.rfind("sha256:", 0) == 0) u.sha256 = lower(digest.substr(7));
        u.notes = j.value("body", "");
        u.page = j.value("html_url", "");
        return u;
    }
    return std::nullopt;
}

Result<fs::path> update_dest() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    fs::path dir = fs::path(tmp) / ("hexwright-update-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(now_ms()));
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return Error{"cannot create a temp folder"};
    return dir;
}

Result<fs::path> download_update(const UpdaterConfig& cfg, const UpdateInfo& info, const ProgressFn& progress) {
    auto dir = update_dest();
    if (!dir.ok()) return dir.error();
    fs::path dest = *dir / widen(cfg.asset_name);
    HttpOptions opt;
    opt.timeout_ms = 30000;
    opt.max_bytes = 512u << 20;
    HttpResponse r = cfg.download(info.url, dest, progress, opt);
    if (r.status != 200) return Error{"download failed: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error)};
    if (!info.sha256.empty()) {
        auto h = sha256_file(dest);
        if (!h || *h != info.sha256) {
            std::error_code ec;
            fs::remove(dest, ec);
            return Error{"the update failed its SHA-256 check and was discarded"};
        }
    }
    return dest;
}

Status apply_update(const fs::path& setup_exe) {
    std::wstring cmd = L"\"" + setup_exe.wstring() + L"\" --silent --update --wait-pid " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi))
        return Error{"could not start the installer (Windows error " + std::to_string(GetLastError()) + ")"};
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return ok_status();
}

}  // namespace hx
