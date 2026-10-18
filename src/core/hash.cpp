#include "core/hash.h"

#include <windows.h>
#include <bcrypt.h>

#include <fstream>
#include <vector>

namespace hx {

namespace {
class Sha256 {
public:
    Sha256() {
        if (BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return;
        if (BCryptCreateHash(alg_, &hash_, nullptr, 0, nullptr, 0, 0) != 0) hash_ = nullptr;
    }
    ~Sha256() {
        if (hash_) BCryptDestroyHash(hash_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }
    void update(const void* d, size_t n) {
        if (hash_ && n) BCryptHashData(hash_, (PUCHAR)d, (ULONG)n, 0);
    }
    std::string finish() {
        uint8_t out[32] = {};
        if (hash_) BCryptFinishHash(hash_, out, sizeof out, 0);
        return hex_encode(out, sizeof out);
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
};
}  // namespace

std::string sha256_hex(const void* data, size_t n) {
    Sha256 h;
    h.update(data, n);
    return h.finish();
}

std::optional<std::string> sha256_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    Sha256 h;
    std::vector<char> buf(1 << 20);
    while (f) {
        f.read(buf.data(), (std::streamsize)buf.size());
        h.update(buf.data(), (size_t)f.gcount());
    }
    return h.finish();
}

std::string sha256_normalized(std::string_view text) {
    std::string t;
    t.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;   // drop the CR of CRLF
        t.push_back(text[i]);
    }
    return sha256_hex(t);
}

std::optional<std::string> sha256_normalized_file(const fs::path& path) {
    auto data = read_file(path);
    if (!data) return std::nullopt;
    return sha256_normalized(*data);
}

}  // namespace hx
