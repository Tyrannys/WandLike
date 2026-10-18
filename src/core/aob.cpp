#include "core/aob.h"

#include <algorithm>
#include <cstring>

#include "core/util.h"

namespace hx {

namespace {
int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}  // namespace

Result<Pattern> Pattern::parse(std::string_view text) {
    Pattern p;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
        if (i >= text.size()) break;
        size_t j = i;
        while (j < text.size() && !std::isspace((unsigned char)text[j])) ++j;
        std::string_view tok = text.substr(i, j - i);
        i = j;
        if (tok == "?" || tok == "??") { p.bytes.push_back(0); p.mask.push_back(0x00); continue; }
        if (tok.size() != 2) return Error{"bad byte \"" + std::string(tok) + "\" in pattern (use two hex digits, ?? or a nibble like 4?)"};
        uint8_t b = 0, m = 0;
        int hi = tok[0] == '?' ? -2 : hex_digit(tok[0]);
        int lo = tok[1] == '?' ? -2 : hex_digit(tok[1]);
        if (hi == -1 || lo == -1) return Error{"bad byte \"" + std::string(tok) + "\" in pattern"};
        if (hi >= 0) { b |= (uint8_t)(hi << 4); m |= 0xF0; }
        if (lo >= 0) { b |= (uint8_t)lo; m |= 0x0F; }
        p.bytes.push_back(b);
        p.mask.push_back(m);
    }
    if (p.bytes.empty()) return Error{"empty pattern"};
    if (std::all_of(p.mask.begin(), p.mask.end(), [](uint8_t m) { return m == 0; })) return Error{"a pattern of only wildcards matches everything"};
    return p;
}

std::string Pattern::to_string() const {
    static const char* hex = "0123456789ABCDEF";
    std::string s;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i) s.push_back(' ');
        if (mask[i] == 0) s += "??";
        else {
            s.push_back(mask[i] & 0xF0 ? hex[bytes[i] >> 4] : '?');
            s.push_back(mask[i] & 0x0F ? hex[bytes[i] & 15] : '?');
        }
    }
    return s;
}

std::vector<size_t> find_in_buffer(const uint8_t* data, size_t n, const Pattern& p, size_t limit) {
    std::vector<size_t> out;
    size_t len = p.size();
    if (len == 0 || n < len || limit == 0) return out;
    // anchor on the first fully-specified byte so memchr can skip most of the buffer
    size_t anchor = len;
    for (size_t i = 0; i < len; ++i)
        if (p.mask[i] == 0xFF) { anchor = i; break; }
    auto matches_at = [&](size_t start) {
        for (size_t i = 0; i < len; ++i)
            if ((data[start + i] & p.mask[i]) != (p.bytes[i] & p.mask[i])) return false;
        return true;
    };
    if (anchor == len) {                                    // only nibble masks: no byte to memchr for
        for (size_t s = 0; s + len <= n && out.size() < limit; ++s)
            if (matches_at(s)) out.push_back(s);
        return out;
    }
    const uint8_t key = p.bytes[anchor];
    size_t pos = anchor;                                    // position of the anchor byte inside the buffer
    while (pos < n && out.size() < limit) {
        const void* hit = std::memchr(data + pos, key, n - pos);
        if (!hit) break;
        size_t at = (size_t)((const uint8_t*)hit - data);
        pos = at + 1;
        if (at < anchor) continue;
        size_t start = at - anchor;
        if (start + len > n) break;
        if (matches_at(start)) out.push_back(start);
    }
    return out;
}

Result<std::vector<uint64_t>> scan_ranges(const Process& proc, const std::vector<AddressRange>& ranges, const Pattern& p, size_t limit) {
    constexpr size_t kChunk = 4u << 20;
    std::vector<uint64_t> out;
    const size_t overlap = p.size() - 1;
    std::vector<uint8_t> buf(kChunk + overlap);
    for (auto& r : ranges) {
        for (uint64_t off = 0; off < r.size && out.size() < limit; off += kChunk) {
            uint64_t span = std::min<uint64_t>(kChunk, r.size - off);
            uint64_t want = std::min<uint64_t>(span + overlap, r.size - off);
            size_t got = proc.read_partial(r.base + off, buf.data(), (size_t)want);
            for (size_t at : find_in_buffer(buf.data(), got, p, limit - out.size())) {
                if (at >= span) continue;                   // belongs to the next chunk (found there)
                out.push_back(r.base + off + at);
            }
        }
    }
    if (!proc.alive()) return Error{"The process has exited."};
    return out;
}

Result<std::vector<uint64_t>> scan_module(const Process& proc, const ModuleInfo& module, std::string_view section, const Pattern& p,
                                          size_t limit) {
    if (section.empty()) return scan_ranges(proc, {{module.base, module.size}}, p, limit);
    auto secs = proc.sections(module.base);
    if (!secs.ok()) return secs.error();
    for (auto& s : *secs)
        if (iequals(s.name, section)) return scan_ranges(proc, {{module.base + s.rva, s.size}}, p, limit);
    return Error{"The module has no section named \"" + std::string(section) + "\"."};
}

std::optional<uint64_t> resolve_rip(const Process& proc, uint64_t match, int disp_at, int insn_len) {
    int32_t disp = 0;
    if (!proc.read(match + (uint64_t)disp_at, &disp, 4)) return std::nullopt;
    return match + (uint64_t)insn_len + (uint64_t)(int64_t)disp;
}

}  // namespace hx
