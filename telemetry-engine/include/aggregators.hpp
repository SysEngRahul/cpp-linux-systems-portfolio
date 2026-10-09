// aggregators.hpp - three ways to combine results from N worker threads.
//
//   LocalAgg  : each worker owns private stats; merged once at the end
//               (no sharing, no synchronisation on the hot path)
//   MutexAgg  : one global table guarded by a single std::mutex
//   AtomicAgg : one global table of std::atomic counters (fetch_add / CAS max)
//
// All three expose: add(worker_id, Sample) and snapshot() -> per-device stats.
// snapshot() must be called after all workers have been joined.
#pragma once

#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include "processing.hpp"

class LocalAgg {
public:
    static constexpr const char* kName = "local";
    LocalAgg(uint32_t devices, unsigned workers)
        : devices_(devices), per_worker_(workers) {
        for (auto& v : per_worker_) v.assign(devices, DeviceStats{});
    }
    void add(unsigned worker, const Sample& s) {
        accumulate(per_worker_[worker][s.device], s);
    }
    std::vector<DeviceStats> snapshot() const {
        std::vector<DeviceStats> out(devices_);
        for (const auto& v : per_worker_)
            for (uint32_t d = 0; d < devices_; ++d) merge_into(out[d], v[d]);
        return out;
    }

private:
    uint32_t devices_;
    std::vector<std::vector<DeviceStats>> per_worker_;
};

class MutexAgg {
public:
    static constexpr const char* kName = "mutex";
    MutexAgg(uint32_t devices, unsigned) : stats_(devices) {}
    void add(unsigned, const Sample& s) {
        std::lock_guard<std::mutex> lk(m_);
        accumulate(stats_[s.device], s);
    }
    std::vector<DeviceStats> snapshot() const {
        std::lock_guard<std::mutex> lk(m_);
        return stats_;
    }

private:
    mutable std::mutex m_;
    std::vector<DeviceStats> stats_;
};

class AtomicAgg {
public:
    static constexpr const char* kName = "atomic";
    AtomicAgg(uint32_t devices, unsigned)
        : devices_(devices), stats_(new Slot[devices]) {
        for (uint32_t i = 0; i < devices; ++i) {
            stats_[i].count.store(0, std::memory_order_relaxed);
            stats_[i].sum_temp.store(0, std::memory_order_relaxed);
            stats_[i].max_temp.store(std::numeric_limits<int32_t>::min(),
                                     std::memory_order_relaxed);
            stats_[i].sum_power.store(0, std::memory_order_relaxed);
            stats_[i].alarms.store(0, std::memory_order_relaxed);
        }
    }
    void add(unsigned, const Sample& s) {
        Slot& d = stats_[s.device];
        d.count.fetch_add(1, std::memory_order_relaxed);
        d.sum_temp.fetch_add(s.temp_x100, std::memory_order_relaxed);
        d.sum_power.fetch_add(s.power_mw, std::memory_order_relaxed);
        if (s.alarm) d.alarms.fetch_add(1, std::memory_order_relaxed);
        int32_t cur = d.max_temp.load(std::memory_order_relaxed);
        while (s.temp_x100 > cur &&
               !d.max_temp.compare_exchange_weak(cur, s.temp_x100,
                                                 std::memory_order_relaxed)) {
        }
    }
    std::vector<DeviceStats> snapshot() const {
        std::vector<DeviceStats> out(devices_);
        for (uint32_t i = 0; i < devices_; ++i) {
            out[i].count = stats_[i].count.load(std::memory_order_relaxed);
            out[i].sum_temp_x100 = stats_[i].sum_temp.load(std::memory_order_relaxed);
            out[i].max_temp_x100 = stats_[i].max_temp.load(std::memory_order_relaxed);
            out[i].sum_power_mw = stats_[i].sum_power.load(std::memory_order_relaxed);
            out[i].alarms = stats_[i].alarms.load(std::memory_order_relaxed);
        }
        return out;
    }

private:
    struct alignas(64) Slot {  // one cache line per device: no false sharing
        std::atomic<uint64_t> count;
        std::atomic<int64_t> sum_temp;
        std::atomic<int32_t> max_temp;
        std::atomic<int64_t> sum_power;
        std::atomic<uint64_t> alarms;
    };
    uint32_t devices_;
    std::unique_ptr<Slot[]> stats_;
};
