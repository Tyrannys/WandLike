#pragma once
// Array-of-bytes signatures with wildcards: find code/data again after a game patch moved it.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/process.h"
#include "core/result.h"

namespace hx {

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> mask;      // per byte: 0xFF exact, 0xF0 / 0x0F one nibble, 0x00 wildcard

    [[nodiscard]] size_t size() const { return bytes.size(); }
    // "48 8B 0D ?? ?? ?? ?? 41 B8"  ('?' or '??' = any byte, '4?' / '?B' = one nibble fixed)
    static Result<Pattern> parse(std::string_view text);
    [[nodiscard]] std::string to_string() const;
};

// Offsets of matches inside a buffer (overlapping matches included), at most `limit`.
std::vector<size_t> find_in_buffer(const uint8_t* data, size_t n, const Pattern& p, size_t limit = 64);

struct AddressRange { uint64_t base = 0; uint64_t size = 0; };

// Scans live memory in chunks (with overlap so matches across chunk edges are found). Addresses are absolute.
Result<std::vector<uint64_t>> scan_ranges(const Process& proc, const std::vector<AddressRange>& ranges, const Pattern& p,
                                          size_t limit = 64);
// Scans one section of a module (e.g. ".text"); an empty section name scans the whole image.
Result<std::vector<uint64_t>> scan_module(const Process& proc, const ModuleInfo& module, std::string_view section, const Pattern& p,
                                          size_t limit = 64);

// Follows a RIP-relative operand: target = match + insn_len + int32 at (match + disp_at).
// Typical: "48 8B 0D <disp32>" is `mov rcx,[rip+disp]`: disp_at 3, insn_len 7.
std::optional<uint64_t> resolve_rip(const Process& proc, uint64_t match_addr, int disp_at, int insn_len);

}  // namespace hx
