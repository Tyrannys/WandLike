#pragma once
// Cheat Engine-style memory scanner: first scan + progressive narrowing.
#include <atomic>
#include <cstdint>
#include <vector>

#include "core/process.h"
#include "core/result.h"
#include "core/value.h"

namespace hx {

enum class ScanOp {
    Exact,          // == v1
    Unknown,        // first scan only: remember every value ("unknown initial value")
    Increased,      // current > previous
    Decreased,      // current < previous
    Changed,        // current != previous
    Unchanged,      // current == previous
    IncreasedBy,    // current - previous == v1
    DecreasedBy,    // previous - current == v1
    Between,        // v1 <= current <= v2
    Bigger,         // current > v1
    Smaller,        // current < v1
};

const char* scan_op_name(ScanOp op);
bool op_needs_previous(ScanOp op);   // Increased/Decreased/Changed/Unchanged/IncreasedBy/DecreasedBy

struct ScanConfig {
    ValueType type = ValueType::I32;
    uint32_t alignment = 0;              // 0 = natural alignment (the type's size); 1 = every byte
    RegionFilter regions;                // default: writable private + image memory
    int float_decimals = -1;             // -1 exact; N >= 0 compares after rounding to N decimals
    uint64_t max_results = 100'000'000;  // refuse to keep more hits than this
    uint64_t max_snapshot_bytes = 2ull << 30;   // cap for "unknown initial value" snapshots
};

struct ScanParams {
    ScanOp op = ScanOp::Exact;
    Value v1;
    Value v2;
};

struct Hit {
    uint64_t address = 0;
    uint64_t previous = 0;               // raw bits of the value at the last scan
};

struct ScanProgress {
    std::atomic<uint64_t> done_bytes{0};
    std::atomic<uint64_t> total_bytes{0};
    std::atomic<bool> cancel{false};
    [[nodiscard]] float fraction() const {
        auto t = total_bytes.load();
        return t ? (float)((double)done_bytes.load() / (double)t) : 0.0f;
    }
};

class Scanner {
public:
    // First scan. With ScanOp::Unknown the memory is snapshotted; every other op records the matching addresses.
    Result<uint64_t> first_scan(const Process& p, const ScanConfig& cfg, const ScanParams& params, ScanProgress* progress = nullptr);
    // Narrow the previous results. Fails if there is no previous scan.
    Result<uint64_t> next_scan(const Process& p, const ScanParams& params, ScanProgress* progress = nullptr);

    [[nodiscard]] bool has_scan() const { return started_; }
    [[nodiscard]] bool is_snapshot() const { return started_ && snapshot_mode_; }
    [[nodiscard]] uint64_t count() const;                // matches (or snapshot elements in snapshot mode)
    [[nodiscard]] const std::vector<Hit>& hits() const { return hits_; }
    [[nodiscard]] const ScanConfig& config() const { return cfg_; }
    void reset();

private:
    struct Snap { uint64_t base = 0; std::vector<uint8_t> bytes; };
    ScanConfig cfg_;
    bool started_ = false;
    bool snapshot_mode_ = false;
    std::vector<Hit> hits_;
    std::vector<Snap> snaps_;
};

}  // namespace hx
