#include "core/pointer.h"

#include "core/util.h"

namespace hx {

namespace {
std::optional<int64_t> parse_signed_hex(std::string_view s) {
    std::string t = trim(s);
    bool neg = false;
    if (!t.empty() && (t[0] == '-' || t[0] == '+')) { neg = t[0] == '-'; t = t.substr(1); }
    auto v = parse_hex(t);
    if (!v) return std::nullopt;
    return neg ? -(int64_t)*v : (int64_t)*v;
}
}  // namespace

Result<PointerChain> PointerChain::parse(std::string_view text) {
    PointerChain c;
    auto parts = split(text, ',');
    if (parts.empty() || trim(parts[0]).empty()) return Error{"empty pointer chain"};
    std::string first = trim(parts[0]);
    size_t plus = first.find('+');
    std::string base_off;
    if (plus != std::string::npos) {
        c.module = trim(first.substr(0, plus));
        base_off = first.substr(plus + 1);
    } else if (parse_hex(first) && first.find('.') == std::string::npos) {
        base_off = first;                                   // absolute address
    } else {
        c.module = first;                                   // bare module name = its base address
        base_off = "0";
    }
    auto b = parse_signed_hex(base_off);
    if (!b) return Error{"bad base offset \"" + base_off + "\""};
    c.offsets.push_back(*b);
    for (size_t i = 1; i < parts.size(); ++i) {
        auto o = parse_signed_hex(parts[i]);
        if (!o) return Error{"bad offset \"" + trim(parts[i]) + "\""};
        c.offsets.push_back(*o);
    }
    return c;
}

std::string PointerChain::format() const {
    auto hex = [](int64_t v) { return std::string(v < 0 ? "-" : "") + format_hex((uint64_t)(v < 0 ? -v : v)); };
    std::string s = module.empty() ? "" : module + "+";
    s += hex(offsets.empty() ? 0 : offsets[0]);
    for (size_t i = 1; i < offsets.size(); ++i) s += "," + hex(offsets[i]);
    return s;
}

Result<uint64_t> resolve_chain(const Process& proc, const PointerChain& chain) {
    if (chain.offsets.empty()) return Error{"empty pointer chain"};
    uint64_t addr = 0;
    if (!chain.module.empty()) {
        auto m = proc.module(chain.module);
        if (!m) return Error{"module \"" + chain.module + "\" is not loaded in the process"};
        addr = m->base;
    }
    addr += (uint64_t)chain.offsets[0];
    for (size_t i = 1; i < chain.offsets.size(); ++i) {
        auto p = proc.read_pointer(addr);
        if (!p) return Error{"could not read a pointer at " + format_hex(addr) + " (level " + std::to_string(i) + ")"};
        if (*p == 0) return Error{"null pointer at level " + std::to_string(i)};
        addr = *p + (uint64_t)chain.offsets[i];
    }
    return addr;
}

}  // namespace hx
