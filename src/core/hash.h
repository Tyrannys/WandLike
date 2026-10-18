#pragma once
#include <optional>
#include <string>
#include <string_view>

#include "core/util.h"

namespace hx {

std::string sha256_hex(const void* data, size_t n);
inline std::string sha256_hex(std::string_view s) { return sha256_hex(s.data(), s.size()); }
std::optional<std::string> sha256_file(const fs::path& path);           // streamed; nullopt if unreadable

// Hash over LF line endings so a Windows checkout and GitHub's raw file agree (used for game definitions).
std::string sha256_normalized(std::string_view text);
std::optional<std::string> sha256_normalized_file(const fs::path& path);

}  // namespace hx
