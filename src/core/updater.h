#pragma once
// Self-update from GitHub Releases: check the latest release, download Hexwright-Setup.exe, run it silently.
#include <functional>
#include <optional>
#include <string>

#include "core/http.h"
#include "core/result.h"

namespace hx {

struct UpdateInfo {
    std::string version;        // "2.1.0"
    std::string url;            // asset download URL (https://github.com/<repo>/releases/download/...)
    std::string sha256;         // from GitHub's asset digest; empty if GitHub did not provide one
    std::string notes;
    std::string page;
};

struct UpdaterConfig {
    std::string repo;
    std::string current_version;
    HttpFetch fetch;
    std::function<HttpResponse(const std::string& url, const fs::path& dest, const ProgressFn&, const HttpOptions&)> download;   // injectable
    std::string asset_name;
};
UpdaterConfig default_updater_config();

// A newer, published (not draft / prerelease) release that carries the setup asset, or nullopt.
// Network or format problems also give nullopt; `error` says why when provided.
std::optional<UpdateInfo> check_for_update(const UpdaterConfig& cfg, std::string* error = nullptr);

// Downloads to a temp folder and verifies the SHA-256 when one is known.
Result<fs::path> download_update(const UpdaterConfig& cfg, const UpdateInfo& info, const ProgressFn& progress = {});

// Starts `Hexwright-Setup.exe --silent --update --wait-pid <our pid>` detached; the caller must exit right after.
Status apply_update(const fs::path& setup_exe);

}  // namespace hx
