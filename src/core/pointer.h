#pragma once
// Multi-level pointer chains that survive game restarts: module base + offset, then follow pointers.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/process.h"
#include "core/result.h"

namespace hx {

struct PointerChain {
    std::string module;                 // "" = the first offset is an absolute address
    std::vector<int64_t> offsets;       // [0] added to the module base; each later offset is applied after dereferencing

    // "ck3.exe+5C7E378,20,8"   module base + 0x5C7E378, read pointer, +0x20, read pointer, +0x8
    // "7FF6FCAA0000,10"        absolute address, read pointer, +0x10
    // All numbers are hexadecimal; offsets may be negative ("-8").
    static Result<PointerChain> parse(std::string_view text);
    [[nodiscard]] std::string format() const;
};

// Returns the final address (the location of the value). Fails if the module is missing or a pointer cannot be read.
Result<uint64_t> resolve_chain(const Process& proc, const PointerChain& chain);

}  // namespace hx
