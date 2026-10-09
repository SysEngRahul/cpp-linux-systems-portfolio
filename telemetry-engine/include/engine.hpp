// engine.hpp - the pipeline:
//   producers -> bounded queue -> worker pool -> filter -> aggregation
#pragma once

#include <pthread.h>
#include <sched.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "aggregators.hpp"
#include "metrics.hpp"
#include "processing.hpp"
#include "queues.hpp"
#include "telemetry.hpp"

enum class QueueKind { Mutex, LockFree };
enum class AggKind { Local, Mutex, Atomic };

struct Config {
    uint64_t records = 5'000'000;   // total records across all producers
    unsigned producers = 2;
    unsigned workers = 4;
    size_t capacity = 65536;        // queue capacity (lockfree: rounded to 2^n)
    QueueKind queue = QueueKind::LockFree;
    AggKind agg = AggKind::Local;
    uint32_t devices = 1024;
    uint64_t seed = 42;
    uint64_t rate = 0;              // records/s PER PRODUCER, 0 = unthrottled
    bool pin = false;               // pin threads to cores round-robin
    bool verify = false;
    bool csv = false;
    bool csv_header = false;
};

struct RunResult {
    uint64_t generated = 0, processed = 0, rejected = 0, alarms = 0;
    double wall_s = 0;
    LatencyHistogram latency;
    ResourceUsage res;  // CPU time consumed during the run (peak_rss absolute)
};

inline uint64_t records_for_producer(const Config& c, unsigned p) {
    return c.records / c.producers + (p < c.records % c.producers ? 1 : 0);
}

inline void pin_thread(std::thread& t, unsigned core) {
    const unsigned ncpu = std::thread::hardware_concurrency();
    if (ncpu == 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core % ncpu, &set);
    pthread_setaffinity_np(t.native_handle(), sizeof(set), &set);
}

template <class Queue, class Agg>
RunResult run_engine(const Config& cfg, Agg& agg) {
    Queue queue(cfg.capacity);

    struct alignas(64) WorkerState {
        uint64_t processed = 0, rejected = 0, alarms = 0;
        LatencyHistogram lat;
    };
    std::vector<WorkerState> ws(cfg.workers);
    std::atomic<bool> go{false};

    auto wait_go = [&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
    };

    auto producer = [&](unsigned p) {
        wait_go();
        Generator gen(cfg.seed, p, cfg.devices);
        const uint64_t n = records_for_producer(cfg, p);
        const uint64_t interval = cfg.rate ? 1'000'000'000ULL / cfg.rate : 0;
        uint64_t next_slot = now_ns();
        for (uint64_t i = 0; i < n; ++i) {
            if (interval) {
                next_slot += interval;
                while (now_ns() < next_slot) cpu_relax();
            }
            Telemetry t = gen.next();
            t.timestamp = now_ns();
            queue.push(t);
        }
    };

    auto worker = [&](unsigned w) {
        wait_go();
        WorkerState& st = ws[w];
        Telemetry t;
        while (queue.pop(t)) {
            Sample s;
            if (evaluate(t, cfg.devices, s)) {
                agg.add(w, s);
                ++st.processed;
                if (s.alarm) ++st.alarms;
            } else {
                ++st.rejected;
            }
            const uint64_t now = now_ns();
            st.lat.record(now > t.timestamp ? now - t.timestamp : 0);
        }
    };

    const ResourceUsage before = read_usage();

    std::vector<std::thread> prod, work;
    for (unsigned w = 0; w < cfg.workers; ++w) work.emplace_back(worker, w);
    for (unsigned p = 0; p < cfg.producers; ++p) prod.emplace_back(producer, p);
    if (cfg.pin) {
        for (unsigned p = 0; p < cfg.producers; ++p) pin_thread(prod[p], p);
        for (unsigned w = 0; w < cfg.workers; ++w) pin_thread(work[w], cfg.producers + w);
    }

    const uint64_t t0 = now_ns();
    go.store(true, std::memory_order_release);
    for (auto& t : prod) t.join();
    queue.close();                       // all producers done -> let workers drain
    for (auto& t : work) t.join();
    const uint64_t t1 = now_ns();

    const ResourceUsage after = read_usage();

    RunResult r;
    r.generated = cfg.records;
    for (const auto& s : ws) {
        r.processed += s.processed;
        r.rejected += s.rejected;
        r.alarms += s.alarms;
        r.latency.merge(s.lat);
    }
    r.wall_s = static_cast<double>(t1 - t0) / 1e9;
    r.res.user_s = after.user_s - before.user_s;
    r.res.sys_s = after.sys_s - before.sys_s;
    r.res.peak_rss_kb = after.peak_rss_kb;
    return r;
}

// Single-threaded, queue-free recomputation of the expected statistics.
inline std::vector<DeviceStats> run_reference(const Config& cfg, uint64_t& rejected) {
    std::vector<DeviceStats> st(cfg.devices);
    rejected = 0;
    for (unsigned p = 0; p < cfg.producers; ++p) {
        Generator gen(cfg.seed, p, cfg.devices);
        const uint64_t n = records_for_producer(cfg, p);
        for (uint64_t i = 0; i < n; ++i) {
            const Telemetry t = gen.next();
            Sample s;
            if (evaluate(t, cfg.devices, s))
                accumulate(st[s.device], s);
            else
                ++rejected;
        }
    }
    return st;
}
