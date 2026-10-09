// main.cpp - CLI front-end: parse options, run, report, verify.
#include <getopt.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "engine.hpp"

namespace {

void usage(const char* prog) {
    std::printf(
        "Usage: %s [options]\n\n"
        "  -n, --records N      total records to generate        (default 5000000)\n"
        "  -p, --producers N    producer threads                 (default 2)\n"
        "  -w, --workers N      worker threads                   (default 4)\n"
        "  -c, --capacity N     queue capacity in records        (default 65536)\n"
        "  -q, --queue KIND     mutex | lockfree                 (default lockfree)\n"
        "  -a, --agg KIND       local | mutex | atomic           (default local)\n"
        "  -d, --devices N      number of distinct device ids    (default 1024)\n"
        "  -s, --seed N         RNG seed                         (default 42)\n"
        "  -r, --rate N         records/s PER PRODUCER, 0 = max  (default 0)\n"
        "      --pin            pin threads to CPU cores\n"
        "      --verify         recompute single-threaded and compare results\n"
        "      --csv            print one CSV line instead of the report\n"
        "      --csv-header     print the CSV header line (use with --csv)\n"
        "  -h, --help           this text\n\n"
        "Numbers accept K/M/G suffixes, e.g. -n 10M -c 64K\n",
        prog);
}

bool parse_u64(const char* s, const char* name, uint64_t& out) {
    if (!s || !std::isdigit(static_cast<unsigned char>(s[0]))) {
        std::fprintf(stderr, "error: --%s needs a non-negative number, got '%s'\n",
                     name, s ? s : "");
        return false;
    }
    char* end = nullptr;
    errno = 0;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (errno == ERANGE) {
        std::fprintf(stderr, "error: --%s value out of range\n", name);
        return false;
    }
    uint64_t mult = 1;
    if (*end) {
        switch (std::toupper(static_cast<unsigned char>(*end))) {
            case 'K': mult = 1000ULL; break;
            case 'M': mult = 1000000ULL; break;
            case 'G': mult = 1000000000ULL; break;
            default: mult = 0;
        }
        if (mult == 0 || end[1] != '\0') {
            std::fprintf(stderr, "error: --%s: bad number '%s'\n", name, s);
            return false;
        }
    }
    out = v * mult;
    return true;
}

template <class Q>
RunResult dispatch_agg(const Config& cfg, std::vector<DeviceStats>& stats) {
    switch (cfg.agg) {
        case AggKind::Local: {
            LocalAgg a(cfg.devices, cfg.workers);
            RunResult r = run_engine<Q>(cfg, a);
            stats = a.snapshot();
            return r;
        }
        case AggKind::Mutex: {
            MutexAgg a(cfg.devices, cfg.workers);
            RunResult r = run_engine<Q>(cfg, a);
            stats = a.snapshot();
            return r;
        }
        case AggKind::Atomic: {
            AtomicAgg a(cfg.devices, cfg.workers);
            RunResult r = run_engine<Q>(cfg, a);
            stats = a.snapshot();
            return r;
        }
    }
    return {};
}

const char* queue_name(QueueKind k) { return k == QueueKind::Mutex ? "mutex" : "lockfree"; }
const char* agg_name(AggKind k) {
    return k == AggKind::Local ? "local" : (k == AggKind::Mutex ? "mutex" : "atomic");
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg;
    static const option opts[] = {
        {"records", 1, nullptr, 'n'},   {"producers", 1, nullptr, 'p'},
        {"workers", 1, nullptr, 'w'},   {"capacity", 1, nullptr, 'c'},
        {"queue", 1, nullptr, 'q'},     {"agg", 1, nullptr, 'a'},
        {"devices", 1, nullptr, 'd'},   {"seed", 1, nullptr, 's'},
        {"rate", 1, nullptr, 'r'},      {"pin", 0, nullptr, 1000},
        {"verify", 0, nullptr, 1001},   {"csv", 0, nullptr, 1002},
        {"csv-header", 0, nullptr, 1003}, {"help", 0, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}};

    int c;
    while ((c = getopt_long(argc, argv, "n:p:w:c:q:a:d:s:r:h", opts, nullptr)) != -1) {
        uint64_t v = 0;
        switch (c) {
            case 'n': if (!parse_u64(optarg, "records", v)) return 2; cfg.records = v; break;
            case 'p': if (!parse_u64(optarg, "producers", v)) return 2; cfg.producers = static_cast<unsigned>(v); break;
            case 'w': if (!parse_u64(optarg, "workers", v)) return 2; cfg.workers = static_cast<unsigned>(v); break;
            case 'c': if (!parse_u64(optarg, "capacity", v)) return 2; cfg.capacity = static_cast<size_t>(v); break;
            case 'd': if (!parse_u64(optarg, "devices", v)) return 2; cfg.devices = static_cast<uint32_t>(v); break;
            case 's': if (!parse_u64(optarg, "seed", v)) return 2; cfg.seed = v; break;
            case 'r': if (!parse_u64(optarg, "rate", v)) return 2; cfg.rate = v; break;
            case 'q':
                if (!std::strcmp(optarg, "mutex")) cfg.queue = QueueKind::Mutex;
                else if (!std::strcmp(optarg, "lockfree")) cfg.queue = QueueKind::LockFree;
                else { std::fprintf(stderr, "error: --queue must be mutex|lockfree\n"); return 2; }
                break;
            case 'a':
                if (!std::strcmp(optarg, "local")) cfg.agg = AggKind::Local;
                else if (!std::strcmp(optarg, "mutex")) cfg.agg = AggKind::Mutex;
                else if (!std::strcmp(optarg, "atomic")) cfg.agg = AggKind::Atomic;
                else { std::fprintf(stderr, "error: --agg must be local|mutex|atomic\n"); return 2; }
                break;
            case 1000: cfg.pin = true; break;
            case 1001: cfg.verify = true; break;
            case 1002: cfg.csv = true; break;
            case 1003: cfg.csv_header = true; break;
            case 'h': usage(argv[0]); return 0;
            default: usage(argv[0]); return 2;
        }
    }
    if (cfg.records == 0 || cfg.producers == 0 || cfg.workers == 0 ||
        cfg.devices == 0 || cfg.capacity == 0 || cfg.producers > 1024 ||
        cfg.workers > 1024) {
        std::fprintf(stderr,
                     "error: records, producers (<=1024), workers (<=1024), devices "
                     "and capacity must all be > 0\n");
        return 2;
    }

    if (cfg.csv_header)
        std::printf("queue,agg,producers,workers,records,capacity,devices,rate,wall_s,"
                    "throughput_mrec_s,processed,rejected,alarms,lat_avg_us,lat_p50_us,"
                    "lat_p99_us,lat_p999_us,lat_max_us,cpu_pct,cpu_user_s,cpu_sys_s,"
                    "peak_rss_mb,verified\n");

    std::vector<DeviceStats> stats;
    RunResult r = (cfg.queue == QueueKind::Mutex)
                      ? dispatch_agg<MutexQueue<Telemetry>>(cfg, stats)
                      : dispatch_agg<LockFreeQueue<Telemetry>>(cfg, stats);

    // ---- sanity: nothing lost, nothing duplicated
    bool ok = (r.processed + r.rejected == cfg.records) &&
              (r.latency.count() == cfg.records);
    uint64_t agg_count = 0, agg_alarms = 0;
    for (const auto& d : stats) { agg_count += d.count; agg_alarms += d.alarms; }
    ok = ok && agg_count == r.processed && agg_alarms == r.alarms;

    // ---- optional: compare against single-threaded reference
    const char* verified = "skipped";
    if (cfg.verify) {
        uint64_t ref_rejected = 0;
        const auto ref = run_reference(cfg, ref_rejected);
        bool same = (ref == stats) && ref_rejected == r.rejected;
        verified = same ? "PASS" : "FAIL";
        ok = ok && same;
    }

    const double throughput = r.generated / r.wall_s / 1e6;
    const double cpu_s = r.res.user_s + r.res.sys_s;
    const double cpu_pct = r.wall_s > 0 ? cpu_s / r.wall_s * 100.0 : 0.0;
    const double rss_mb = r.res.peak_rss_kb / 1024.0;
    auto us = [](uint64_t ns) { return ns / 1000.0; };

    if (cfg.csv) {
        std::printf("%s,%s,%u,%u,%llu,%zu,%u,%llu,%.4f,%.3f,%llu,%llu,%llu,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.3f,%.3f,%.1f,%s\n",
                    queue_name(cfg.queue), agg_name(cfg.agg), cfg.producers, cfg.workers,
                    (unsigned long long)cfg.records, cfg.capacity, cfg.devices,
                    (unsigned long long)cfg.rate, r.wall_s, throughput,
                    (unsigned long long)r.processed, (unsigned long long)r.rejected,
                    (unsigned long long)r.alarms, r.latency.mean() / 1000.0,
                    us(r.latency.percentile(50)), us(r.latency.percentile(99)),
                    us(r.latency.percentile(99.9)), us(r.latency.max()), cpu_pct,
                    r.res.user_s, r.res.sys_s, rss_mb, verified);
        return ok ? 0 : 1;
    }

    // ---- human readable report
    uint64_t total_n = 0;
    int64_t total_t = 0, total_p = 0;
    int32_t max_t = INT32_MIN;
    for (const auto& d : stats) {
        total_n += d.count;
        total_t += d.sum_temp_x100;
        total_p += d.sum_power_mw;
        if (d.count && d.max_temp_x100 > max_t) max_t = d.max_temp_x100;
    }
    std::vector<uint32_t> idx(stats.size());
    std::iota(idx.begin(), idx.end(), 0u);
    std::sort(idx.begin(), idx.end(), [&](uint32_t a, uint32_t b) {
        if (stats[a].max_temp_x100 != stats[b].max_temp_x100)
            return stats[a].max_temp_x100 > stats[b].max_temp_x100;
        return a < b;
    });

    std::printf("=== Telemetry Processing Engine ===\n");
    std::printf("Config\n");
    std::printf("  queue / aggregation : %s / %s\n", queue_name(cfg.queue), agg_name(cfg.agg));
    std::printf("  producers / workers : %u / %u%s\n", cfg.producers, cfg.workers,
                cfg.pin ? "  (pinned)" : "");
    std::printf("  records             : %llu  (devices: %u, queue capacity: %zu)\n",
                (unsigned long long)cfg.records, cfg.devices, cfg.capacity);
    std::printf("  producer rate       : %s\n",
                cfg.rate ? (std::to_string(cfg.rate) + " rec/s each").c_str() : "unthrottled");
    std::printf("  hardware threads    : %u\n\n", std::thread::hardware_concurrency());

    std::printf("Throughput\n");
    std::printf("  wall time           : %.3f s\n", r.wall_s);
    std::printf("  throughput          : %.2f M records/s\n\n", throughput);

    std::printf("Latency (generation -> processed, includes queueing/backpressure)\n");
    std::printf("  avg / p50           : %.2f / %.2f us\n", r.latency.mean() / 1000.0,
                us(r.latency.percentile(50)));
    std::printf("  p99 / p99.9         : %.2f / %.2f us\n", us(r.latency.percentile(99)),
                us(r.latency.percentile(99.9)));
    std::printf("  max                 : %.2f us\n\n", us(r.latency.max()));

    std::printf("Resources\n");
    std::printf("  CPU time            : %.3f s user + %.3f s sys\n", r.res.user_s, r.res.sys_s);
    std::printf("  CPU utilisation     : %.0f%% (100%% = one fully busy core)\n", cpu_pct);
    std::printf("  peak RSS            : %.1f MB\n\n", rss_mb);

    std::printf("Results\n");
    std::printf("  processed / rejected: %llu / %llu\n", (unsigned long long)r.processed,
                (unsigned long long)r.rejected);
    std::printf("  over-temp alarms    : %llu (> %.0f C)\n", (unsigned long long)r.alarms,
                kAlarmTempC);
    if (total_n) {
        std::printf("  mean temperature    : %.2f C\n", total_t / 100.0 / total_n);
        std::printf("  mean power          : %.1f mW\n", static_cast<double>(total_p) / total_n);
        std::printf("  max temperature     : %.2f C\n", max_t / 100.0);
    }
    std::printf("  hottest devices     :");
    for (size_t i = 0; i < std::min<size_t>(5, idx.size()); ++i)
        if (stats[idx[i]].count)
            std::printf(" #%u(%.2fC)", idx[i], stats[idx[i]].max_temp_x100 / 100.0);
    std::printf("\n");
    std::printf("  integrity check     : %s\n", (r.processed + r.rejected == cfg.records) ? "OK (no loss)" : "FAILED");
    std::printf("  verify vs reference : %s\n", verified);

    return ok ? 0 : 1;
}
