#include "core/scanner.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hx {

const char* scan_op_name(ScanOp op) {
    switch (op) {
        case ScanOp::Exact: return "Exact value";
        case ScanOp::Unknown: return "Unknown initial value";
        case ScanOp::Increased: return "Increased value";
        case ScanOp::Decreased: return "Decreased value";
        case ScanOp::Changed: return "Changed value";
        case ScanOp::Unchanged: return "Unchanged value";
        case ScanOp::IncreasedBy: return "Increased by";
        case ScanOp::DecreasedBy: return "Decreased by";
        case ScanOp::Between: return "Value between";
        case ScanOp::Bigger: return "Bigger than";
        default: return "Smaller than";
    }
}

bool op_needs_previous(ScanOp op) {
    switch (op) {
        case ScanOp::Increased: case ScanOp::Decreased: case ScanOp::Changed: case ScanOp::Unchanged:
        case ScanOp::IncreasedBy: case ScanOp::DecreasedBy: return true;
        default: return false;
    }
}

namespace {

constexpr size_t kChunk = 8u << 20;      // bytes read per ReadProcessMemory call during a region walk
constexpr size_t kBlock = 64u << 10;     // window used to batch nearby hits in a next scan

template <class T> T load(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }
template <class T> T from_bits(uint64_t bits) { T v; std::memcpy(&v, &bits, sizeof v); return v; }
template <class T> uint64_t to_bits(T v) { uint64_t b = 0; std::memcpy(&b, &v, sizeof v); return b; }

template <class T>
struct Matcher {
    ScanOp op = ScanOp::Exact;
    T a{}, b{};
    int decimals = -1;

    [[nodiscard]] static bool feq(T x, T y, int d) {
        if constexpr (std::is_floating_point_v<T>) {
            if (d < 0) return x == y;
            double k = std::pow(10.0, d);
            return std::llround((double)x * k) == std::llround((double)y * k);
        } else {
            (void)d;
            return x == y;
        }
    }
    [[nodiscard]] bool vs_const(T cur) const {
        switch (op) {
            case ScanOp::Exact: return feq(cur, a, decimals);
            case ScanOp::Bigger: return cur > a;
            case ScanOp::Smaller: return cur < a;
            case ScanOp::Between: return cur >= a && cur <= b;
            default: return false;
        }
    }
    [[nodiscard]] bool vs_prev(T cur, T prev) const {
        switch (op) {
            case ScanOp::Increased: return cur > prev && !feq(cur, prev, decimals);
            case ScanOp::Decreased: return cur < prev && !feq(cur, prev, decimals);
            case ScanOp::Changed: return !feq(cur, prev, decimals);
            case ScanOp::Unchanged: return feq(cur, prev, decimals);
            case ScanOp::IncreasedBy:
                if constexpr (std::is_floating_point_v<T>) return feq((T)(cur - prev), a, decimals);
                else return (int64_t)cur - (int64_t)prev == (int64_t)a;
            case ScanOp::DecreasedBy:
                if constexpr (std::is_floating_point_v<T>) return feq((T)(prev - cur), a, decimals);
                else return (int64_t)prev - (int64_t)cur == (int64_t)a;
            default: return false;
        }
    }
};

template <class T>
Matcher<T> make_matcher(const ScanConfig& cfg, const ScanParams& p) {
    Matcher<T> m;
    m.op = p.op;
    m.a = from_bits<T>(p.v1.bits);
    m.b = from_bits<T>(p.v2.bits);
    m.decimals = cfg.float_decimals;
    return m;
}

template <class F>
auto dispatch(ValueType t, F&& f) {
    switch (t) {
        case ValueType::U8: return f(uint8_t{});
        case ValueType::I16: return f(int16_t{});
        case ValueType::I32: return f(int32_t{});
        case ValueType::I64: return f(int64_t{});
        case ValueType::F32: return f(float{});
        default: return f(double{});
    }
}

uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

bool cancelled(ScanProgress* p) { return p && p->cancel.load(); }

// First scan against a constant (Exact / Between / Bigger / Smaller): keep the addresses that match.
template <class T>
Result<uint64_t> first_const(const Process& proc, const ScanConfig& cfg, const Matcher<T>& m, size_t stride, std::vector<Hit>& out,
                             ScanProgress* prog) {
    auto regs = proc.regions(cfg.regions);
    if (prog) {
        uint64_t total = 0;
        for (auto& r : regs) total += r.size;
        prog->total_bytes = total;
        prog->done_bytes = 0;
    }
    std::vector<uint8_t> buf(kChunk + sizeof(T));
    for (auto& r : regs) {
        for (uint64_t off = 0; off < r.size; off += kChunk) {
            if (cancelled(prog)) return Error{"cancelled"};
            uint64_t span = std::min<uint64_t>(kChunk, r.size - off);
            uint64_t want = std::min<uint64_t>(span + (stride < sizeof(T) ? sizeof(T) - 1 : 0), r.size - off);
            size_t got = proc.read_partial(r.base + off, buf.data(), (size_t)want);
            uint64_t chunk_base = r.base + off;
            for (uint64_t a = align_up(chunk_base, stride); a < chunk_base + span; a += stride) {
                size_t i = (size_t)(a - chunk_base);
                if (i + sizeof(T) > got) break;
                T v = load<T>(buf.data() + i);
                if (m.vs_const(v)) {
                    out.push_back({a, to_bits(v)});
                    if (out.size() > cfg.max_results) return Error{"Too many results (over " + std::to_string(cfg.max_results) +
                                                                    "). Narrow the memory range or use a more specific value."};
                }
            }
            if (prog) prog->done_bytes += span;
        }
    }
    return (uint64_t)out.size();
}

template <class T>
Result<uint64_t> next_from_hits(const Process& proc, const ScanConfig& cfg, const Matcher<T>& m, std::vector<Hit>& hits,
                                ScanProgress* prog) {
    (void)cfg;
    const bool needs_prev = op_needs_previous(m.op);
    std::vector<Hit> out;
    out.reserve(hits.size() / 2 + 16);
    std::vector<uint8_t> buf;
    if (prog) { prog->total_bytes = hits.size(); prog->done_bytes = 0; }
    size_t i = 0;
    while (i < hits.size()) {
        if (cancelled(prog)) return Error{"cancelled"};
        uint64_t start = hits[i].address;
        size_t j = i;
        while (j < hits.size() && hits[j].address + sizeof(T) - start <= kBlock) ++j;
        if (j == i) j = i + 1;
        uint64_t end = hits[j - 1].address + sizeof(T);
        buf.resize((size_t)(end - start));
        size_t got = proc.read_partial(start, buf.data(), buf.size());
        for (size_t k = i; k < j; ++k) {
            size_t off = (size_t)(hits[k].address - start);
            if (off + sizeof(T) > got) continue;                 // no longer readable: drop it
            T cur = load<T>(buf.data() + off);
            bool match = needs_prev ? m.vs_prev(cur, from_bits<T>(hits[k].previous)) : m.vs_const(cur);
            if (match) out.push_back({hits[k].address, to_bits(cur)});
        }
        if (prog) prog->done_bytes += (j - i);
        i = j;
    }
    hits = std::move(out);
    return (uint64_t)hits.size();
}

}  // namespace

uint64_t Scanner::count() const {
    if (!started_) return 0;
    if (!snapshot_mode_) return hits_.size();
    uint64_t stride = cfg_.alignment ? cfg_.alignment : type_size(cfg_.type);
    uint64_t n = 0;
    for (auto& s : snaps_) n += s.bytes.size() >= type_size(cfg_.type) ? (s.bytes.size() - type_size(cfg_.type)) / stride + 1 : 0;
    return n;
}

void Scanner::reset() {
    started_ = false;
    snapshot_mode_ = false;
    hits_.clear();
    hits_.shrink_to_fit();
    snaps_.clear();
    snaps_.shrink_to_fit();
}

Result<uint64_t> Scanner::first_scan(const Process& proc, const ScanConfig& cfg, const ScanParams& params, ScanProgress* prog) {
    if (op_needs_previous(params.op)) return Error{"That scan type needs a previous scan to compare against."};
    if (params.op != ScanOp::Unknown) {
        if (params.v1.type != cfg.type) return Error{"The value type does not match the scan type."};
        if (params.op == ScanOp::Between && params.v2.type != cfg.type) return Error{"The second value type does not match."};
    }
    ScanConfig c = cfg;
    size_t size = type_size(c.type);
    if (c.alignment == 0) c.alignment = (uint32_t)size;
    if (c.alignment == 0 || (c.alignment & (c.alignment - 1)) != 0) return Error{"Alignment must be a power of two."};

    std::vector<Hit> hits;
    std::vector<Snap> snaps;
    if (params.op == ScanOp::Unknown) {
        auto regs = proc.regions(c.regions);
        uint64_t total = 0;
        for (auto& r : regs) total += r.size;
        if (total > c.max_snapshot_bytes)
            return Error{"An unknown-value scan would snapshot " + std::to_string(total >> 20) + " MB, over the " +
                         std::to_string(c.max_snapshot_bytes >> 20) + " MB limit. Restrict the memory range or scan for a value first."};
        if (prog) { prog->total_bytes = total; prog->done_bytes = 0; }
        for (auto& r : regs) {
            for (uint64_t off = 0; off < r.size; off += kChunk) {
                if (cancelled(prog)) return Error{"cancelled"};
                uint64_t span = std::min<uint64_t>(kChunk, r.size - off);
                Snap s;
                s.base = r.base + off;
                s.bytes.resize((size_t)span);
                size_t got = proc.read_partial(s.base, s.bytes.data(), (size_t)span);
                s.bytes.resize(got);
                if (got >= size) snaps.push_back(std::move(s));
                if (prog) prog->done_bytes += span;
            }
        }
    } else {
        auto r = dispatch(c.type, [&](auto tag) -> Result<uint64_t> {
            using T = decltype(tag);
            return first_const<T>(proc, c, make_matcher<T>(c, params), c.alignment, hits, prog);
        });
        if (!r.ok()) return r;
    }
    reset();
    cfg_ = c;
    started_ = true;
    snapshot_mode_ = params.op == ScanOp::Unknown;
    hits_ = std::move(hits);
    snaps_ = std::move(snaps);
    return count();
}

Result<uint64_t> Scanner::next_scan(const Process& proc, const ScanParams& params, ScanProgress* prog) {
    if (!started_) return Error{"Run a first scan before narrowing."};
    if (params.op == ScanOp::Unknown) return Error{"\"Unknown initial value\" can only start a scan."};
    bool needs_const = !op_needs_previous(params.op);
    if (needs_const && params.v1.type != cfg_.type) return Error{"The value type does not match the scan type."};
    if (params.op == ScanOp::Between && params.v2.type != cfg_.type) return Error{"The second value type does not match."};
    if (params.op == ScanOp::IncreasedBy || params.op == ScanOp::DecreasedBy)
        if (params.v1.type != cfg_.type) return Error{"The value type does not match the scan type."};
    if (!proc.alive()) return Error{"The process has exited."};

    if (!snapshot_mode_) {
        return dispatch(cfg_.type, [&](auto tag) -> Result<uint64_t> {
            using T = decltype(tag);
            return next_from_hits<T>(proc, cfg_, make_matcher<T>(cfg_, params), hits_, prog);
        });
    }

    // Snapshot mode: compare the live memory with the snapshot and keep only the matching addresses from now on.
    const size_t size = type_size(cfg_.type);
    const uint64_t stride = cfg_.alignment ? cfg_.alignment : size;
    std::vector<Hit> out;
    auto r = dispatch(cfg_.type, [&](auto tag) -> Result<uint64_t> {
        using T = decltype(tag);
        Matcher<T> m = make_matcher<T>(cfg_, params);
        std::vector<uint8_t> live;
        if (prog) {
            uint64_t total = 0;
            for (auto& s : snaps_) total += s.bytes.size();
            prog->total_bytes = total;
            prog->done_bytes = 0;
        }
        for (auto& s : snaps_) {
            if (cancelled(prog)) return Error{"cancelled"};
            live.resize(s.bytes.size());
            size_t got = proc.read_partial(s.base, live.data(), live.size());
            for (uint64_t a = align_up(s.base, stride); a + sizeof(T) <= s.base + s.bytes.size(); a += stride) {
                size_t i = (size_t)(a - s.base);
                if (i + sizeof(T) > got) break;
                T cur = load<T>(live.data() + i);
                bool match = m.op == ScanOp::Exact || m.op == ScanOp::Bigger || m.op == ScanOp::Smaller || m.op == ScanOp::Between
                                 ? m.vs_const(cur)
                                 : m.vs_prev(cur, load<T>(s.bytes.data() + i));
                if (match) {
                    out.push_back({a, to_bits(cur)});
                    if (out.size() > cfg_.max_results)
                        return Error{"Too many results (over " + std::to_string(cfg_.max_results) + "). Use a more specific scan."};
                }
            }
            if (prog) prog->done_bytes += s.bytes.size();
        }
        return (uint64_t)out.size();
    });
    if (!r.ok()) return r;
    hits_ = std::move(out);
    snaps_.clear();
    snaps_.shrink_to_fit();
    snapshot_mode_ = false;
    return (uint64_t)hits_.size();
}

}  // namespace hx
