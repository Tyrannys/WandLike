#include "core/http.h"

#include <windows.h>
#include <winhttp.h>

#include <fstream>
#include <memory>

#include "hx/version.h"

namespace hx {

namespace {

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET x) : h(x) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    explicit operator bool() const { return h != nullptr; }
};

std::string win_error(const char* what) {
    return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")";
}

void parse_headers(const std::wstring& raw, std::map<std::string, std::string>& out) {
    std::string s = narrow(raw);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t eol = s.find("\r\n", pos);
        if (eol == std::string::npos) eol = s.size();
        std::string line = s.substr(pos, eol - pos);
        pos = eol + 2;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        out[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }
}

// Shared GET machinery; `sink` receives body chunks and may return false to abort.
HttpResponse do_get(const std::string& url, const HttpOptions& opt,
                    const std::function<bool(const char*, size_t, uint64_t total)>& sink) {
    HttpResponse r;
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;  uc.dwHostNameLength = (DWORD)std::size(host);
    uc.lpszUrlPath = path;   uc.dwUrlPathLength = (DWORD)std::size(path);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { r.error = "bad URL"; return r; }
    if (uc.nScheme != INTERNET_SCHEME_HTTPS && uc.nScheme != INTERNET_SCHEME_HTTP) { r.error = "unsupported URL scheme"; return r; }

    std::wstring ua = widen(std::string(HX_APP_NAME) + "/" + HX_VERSION_STRING);
    Handle session(WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0));
    if (!session) { r.error = win_error("WinHttpOpen"); return r; }
    WinHttpSetTimeouts(session.h, opt.timeout_ms, opt.timeout_ms, opt.timeout_ms, opt.timeout_ms);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);

    Handle conn(WinHttpConnect(session.h, host, uc.nPort, 0));
    if (!conn) { r.error = win_error("WinHttpConnect"); return r; }
    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle req(WinHttpOpenRequest(conn.h, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!req) { r.error = win_error("WinHttpOpenRequest"); return r; }

    std::wstring extra;
    for (auto& [k, v] : opt.headers) extra += widen(k) + L": " + widen(v) + L"\r\n";
    if (!WinHttpSendRequest(req.h, extra.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extra.c_str(),
                            extra.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        r.error = win_error("WinHttpSendRequest");
        return r;
    }
    if (!WinHttpReceiveResponse(req.h, nullptr)) { r.error = win_error("WinHttpReceiveResponse"); return r; }

    DWORD status = 0, sz = sizeof status;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                        WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;

    DWORD raw_size = 0;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &raw_size,
                        WINHTTP_NO_HEADER_INDEX);
    if (raw_size) {
        std::wstring raw(raw_size / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &raw_size,
                                WINHTTP_NO_HEADER_INDEX))
            parse_headers(raw, r.headers);
    }
    uint64_t total = 0;
    if (auto cl = r.header("content-length"); !cl.empty()) total = std::strtoull(cl.c_str(), nullptr, 10);
    if (total > opt.max_bytes) { r.error = "response too large"; r.status = 0; return r; }

    std::string chunk;
    uint64_t received = 0;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail)) { r.error = win_error("WinHttpQueryDataAvailable"); r.status = 0; return r; }
        if (avail == 0) break;
        chunk.resize(avail);
        DWORD got = 0;
        if (!WinHttpReadData(req.h, chunk.data(), avail, &got)) { r.error = win_error("WinHttpReadData"); r.status = 0; return r; }
        received += got;
        if (received > opt.max_bytes) { r.error = "response too large"; r.status = 0; return r; }
        if (!sink(chunk.data(), got, total)) { r.error = "aborted"; r.status = 0; return r; }
    }
    return r;
}

}  // namespace

HttpResponse http_get(const std::string& url, const HttpOptions& opt) {
    std::string body;
    HttpResponse r = do_get(url, opt, [&](const char* d, size_t n, uint64_t) { body.append(d, n); return true; });
    if (r.status != 0) r.body = std::move(body);
    return r;
}

HttpResponse http_download(const std::string& url, const fs::path& dest, const ProgressFn& progress, const HttpOptions& opt) {
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    fs::path tmp = dest;
    tmp += L".part";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) { HttpResponse r; r.error = "cannot write " + narrow(tmp.wstring()); return r; }
    uint64_t done = 0;
    bool write_failed = false;
    HttpResponse r = do_get(url, opt, [&](const char* d, size_t n, uint64_t total) {
        out.write(d, (std::streamsize)n);
        if (!out) { write_failed = true; return false; }
        done += n;
        if (progress) progress(done, total);
        return true;
    });
    out.close();
    if (r.status == 200 && !write_failed) {
        if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) { r.status = 0; r.error = "cannot finalise download"; }
    } else {
        fs::remove(tmp, ec);
        if (write_failed && r.error.empty()) r.error = "disk write failed";
    }
    return r;
}

}  // namespace hx
