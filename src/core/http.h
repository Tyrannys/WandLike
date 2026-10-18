#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "core/util.h"

namespace hx {

struct HttpOptions {
    int timeout_ms = 15000;
    size_t max_bytes = 64u << 20;                       // abort bodies larger than this
    std::map<std::string, std::string> headers;         // extra request headers
};

struct HttpResponse {
    int status = 0;                                     // 0 = transport failure (see error)
    std::string body;
    std::map<std::string, std::string> headers;         // lower-cased names
    std::string error;
    [[nodiscard]] std::string header(const std::string& lower_name) const {
        auto it = headers.find(lower_name);
        return it == headers.end() ? std::string() : it->second;
    }
};

using HttpFetch = std::function<HttpResponse(const std::string& url, const HttpOptions&)>;   // injectable for tests

HttpResponse http_get(const std::string& url, const HttpOptions& opt = {});
using ProgressFn = std::function<void(uint64_t done, uint64_t total)>;
// Streams the body to `dest` (a temp file is renamed on success). `body` stays empty.
HttpResponse http_download(const std::string& url, const fs::path& dest, const ProgressFn& progress = {},
                           const HttpOptions& opt = {});

}  // namespace hx
