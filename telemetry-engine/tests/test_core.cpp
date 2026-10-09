// test_core.cpp - self-contained unit/stress tests (no framework needed).
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "aggregators.hpp"
#include "metrics.hpp"
#include "processing.hpp"
#include "queues.hpp"

static int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_fail;                                                        \
        }                                                                    \
    } while (0)

// P producers push distinct ids; C consumers pop; every id must arrive once.
template <class Q>
static void queue_stress(const char* name, unsigned P, unsigned C, size_t cap, uint32_t per) {
    Q q(cap);
    const uint64_t total = static_cast<uint64_t>(P) * per;
    std::vector<std::atomic<uint8_t>> seen(total);
    for (auto& s : seen) s.store(0);
    std::atomic<uint64_t> popped{0};
    std::vector<std::thread> prod, cons;
    for (unsigned c = 0; c < C; ++c)
        cons.emplace_back([&] {
            Telemetry t;
            while (q.pop(t)) {
                seen[t.device_id].fetch_add(1);
                popped.fetch_add(1);
            }
        });
    for (unsigned p = 0; p < P; ++p)
        prod.emplace_back([&, p] {
            for (uint32_t i = 0; i < per; ++i) {
                Telemetry t{};
                t.device_id = p * per + i;
                q.push(t);
            }
        });
    for (auto& t : prod) t.join();
    q.close();
    for (auto& t : cons) t.join();
    CHECK(popped.load() == total);
    uint64_t bad = 0;
    for (auto& s : seen) bad += (s.load() != 1);
    CHECK(bad == 0);
    std::printf("  queue %-9s P=%u C=%u cap=%zu: %s\n", name, P, C, cap, (bad == 0 && popped == total) ? "ok" : "FAILED");
}

static void test_queues() {
    std::printf("queues\n");
    for (unsigned P : {1u, 3u})
        for (unsigned C : {1u, 4u})
            for (size_t cap : {size_t{1}, size_t{7}, size_t{1024}}) {
                queue_stress<MutexQueue<Telemetry>>("mutex", P, C, cap, 20000);
                queue_stress<LockFreeQueue<Telemetry>>("lockfree", P, C, cap, 20000);
            }
    // pop on a closed, empty queue must return false immediately
    MutexQueue<Telemetry> a(4);
    LockFreeQueue<Telemetry> b(4);
    a.close();
    b.close();
    Telemetry t;
    CHECK(!a.pop(t));
    CHECK(!b.pop(t));
}

static void test_histogram() {
    std::printf("histogram\n");
    LatencyHistogram h;
    for (uint64_t v = 1; v <= 100000; ++v) h.record(v);
    CHECK(h.count() == 100000);
    CHECK(h.max() == 100000);
    auto near = [](uint64_t got, double want) {
        return got >= want * 0.93 && got <= want * 1.07;
    };
    CHECK(near(h.percentile(50), 50000));
    CHECK(near(h.percentile(99), 99000));
    CHECK(h.percentile(100) <= 100000);
    LatencyHistogram e;
    CHECK(e.percentile(50) == 0);
    LatencyHistogram small;
    small.record(3);
    CHECK(small.percentile(50) == 3);
    LatencyHistogram big;
    big.record(UINT64_MAX);  // must not index out of range
    CHECK(big.count() == 1);
}

static void test_processing() {
    std::printf("processing\n");
    Sample s;
    Telemetry ok{0, 5, 25.0f, 5.0f, 2.0f};
    CHECK(evaluate(ok, 10, s));
    CHECK(s.temp_x100 == 2500);
    CHECK(s.power_mw == 10000);
    CHECK(!s.alarm);
    Telemetry hot = ok;
    hot.temperature = 90.0f;
    CHECK(evaluate(hot, 10, s) && s.alarm);
    Telemetry bad = ok;
    bad.temperature = 999.0f;
    CHECK(!evaluate(bad, 10, s));
    bad = ok;
    bad.voltage = NAN;
    CHECK(!evaluate(bad, 10, s));
    bad = ok;
    bad.device_id = 10;  // out of range
    CHECK(!evaluate(bad, 10, s));

    // generator is deterministic per (seed, producer)
    Generator g1(7, 0, 100), g2(7, 0, 100), g3(7, 1, 100);
    bool same = true, diff = false;
    for (int i = 0; i < 1000; ++i) {
        Telemetry a = g1.next(), b = g2.next(), c = g3.next();
        same &= (a.device_id == b.device_id && a.temperature == b.temperature);
        diff |= (a.device_id != c.device_id || a.temperature != c.temperature);
    }
    CHECK(same);
    CHECK(diff);
}

// All three aggregators must produce identical results under contention.
static void test_aggregators() {
    std::printf("aggregators\n");
    const uint32_t devices = 16;
    const unsigned W = 4;
    LocalAgg la(devices, W);
    MutexAgg ma(devices, W);
    AtomicAgg aa(devices, W);
    std::vector<DeviceStats> expect(devices);
    for (unsigned w = 0; w < W; ++w)
        for (int i = 0; i < 50000; ++i) {
            Sample s{static_cast<uint32_t>((i * 7 + w) % devices),
                     static_cast<int32_t>((i * 13 + w * 101) % 12000 - 2000),
                     static_cast<int64_t>(i % 5000), (i % 11) == 0};
            accumulate(expect[s.device], s);
        }
    std::vector<std::thread> th;
    for (unsigned w = 0; w < W; ++w)
        th.emplace_back([&, w] {
            for (int i = 0; i < 50000; ++i) {
                Sample s{static_cast<uint32_t>((i * 7 + w) % devices),
                         static_cast<int32_t>((i * 13 + w * 101) % 12000 - 2000),
                         static_cast<int64_t>(i % 5000), (i % 11) == 0};
                la.add(w, s);
                ma.add(w, s);
                aa.add(w, s);
            }
        });
    for (auto& t : th) t.join();
    CHECK(la.snapshot() == expect);
    CHECK(ma.snapshot() == expect);
    CHECK(aa.snapshot() == expect);
}

int main() {
    test_processing();
    test_histogram();
    test_aggregators();
    test_queues();
    if (g_fail) {
        std::fprintf(stderr, "\n%d check(s) FAILED\n", g_fail);
        return 1;
    }
    std::printf("\nALL TESTS PASSED\n");
    return 0;
}
