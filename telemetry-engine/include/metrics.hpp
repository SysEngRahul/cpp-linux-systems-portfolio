// metrics.hpp - latency histogram + CPU / memory accounting.
#pragma once

#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Log-linear histogram (16 sub-buckets per power of two, ~3% resolution).
// Each worker owns one (no sharing); they are merged after the run.
class LatencyHistogram {
public:
    static constexpr size_t kBuckets = 1024;

    void record(uint64_t ns) {
        ++b_[index(ns)];
        ++count_;
        sum_ += ns;
        if (ns < min_) min_ = ns;
        if (ns > max_) max_ = ns;
    }

    void merge(const LatencyHistogram& o) {
        for (size_t i = 0; i < kBuckets; ++i) b_[i] += o.b_[i];
        count_ += o.count_;
        sum_ += o.sum_;
        min_ = std::min(min_, o.min_);
        max_ = std::max(max_, o.max_);
    }

    uint64_t count() const { return count_; }
    uint64_t max() const { return count_ ? max_ : 0; }
    double mean() const { return count_ ? static_cast<double>(sum_) / count_ : 0.0; }

    // p in (0,100]; returns nanoseconds (bucket midpoint, clamped to min/max).
    uint64_t percentile(double p) const {
        if (!count_) return 0;
        uint64_t target = static_cast<uint64_t>(std::ceil(p / 100.0 * count_));
        if (target < 1) target = 1;
        uint64_t cum = 0;
        for (size_t i = 0; i < kBuckets; ++i) {
            cum += b_[i];
            if (cum >= target) return std::clamp(midpoint(i), min_, max_);
        }
        return max_;
    }

private:
    static size_t index(uint64_t ns) {
        if (ns < 16) return static_cast<size_t>(ns);
        const int msb = 63 - __builtin_clzll(ns);
        return static_cast<size_t>((msb - 3) * 16 + ((ns >> (msb - 4)) & 15));
    }
    static uint64_t midpoint(size_t idx) {
        if (idx < 16) return idx;
        const unsigned shift = static_cast<unsigned>(idx / 16 - 1);
        const uint64_t lower = static_cast<uint64_t>(16 + idx % 16) << shift;
        return lower + ((uint64_t{1} << shift) >> 1);
    }

    std::array<uint64_t, kBuckets> b_{};
    uint64_t count_ = 0, sum_ = 0, max_ = 0;
    uint64_t min_ = UINT64_MAX;
};

struct ResourceUsage {
    double user_s = 0, sys_s = 0;
    long peak_rss_kb = 0;  // process lifetime peak (Linux: kilobytes)
};

inline ResourceUsage read_usage() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    ResourceUsage r;
    r.user_s = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6;
    r.sys_s = ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
    r.peak_rss_kb = ru.ru_maxrss;
    return r;
}
