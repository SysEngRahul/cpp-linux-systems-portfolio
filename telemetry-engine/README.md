# High-Performance Telemetry Processing Engine (C++17)

A multithreaded pipeline that generates millions of fake sensor records, pushes
them through a bounded concurrent queue to a worker pool, filters and
aggregates them, and reports **throughput, latency percentiles, CPU
utilisation and memory usage**. It lets you compare:

* **queue implementations** - mutex + condition variables vs. a lock-free MPMC ring buffer
* **aggregation strategies** - per-worker (thread-local) vs. one global mutex vs. global atomics
* **thread counts** - any number of producers and workers
* optional **CPU pinning** and **rate-limited producers**

Results are **verified**: every run can be checked against a single-threaded
reference implementation, bit-for-bit.

```
 Producer Threads  (generate Telemetry, stamp creation time)
        │
        ▼
 Bounded Concurrent Queue      --queue  mutex | lockfree
        │
        ▼
 Worker Thread Pool
        │
        ▼
 Filter / Processing           reject sensor faults, flag over-temp (>85 C),
        │                      convert to fixed-point
        ▼
 Aggregation                   --agg  local | mutex | atomic   (per-device stats)
        │
        ▼
 Performance Metrics           throughput, latency histogram, CPU, peak RSS
```

The record type:

```cpp
struct Telemetry {
    uint64_t timestamp;   // creation time (steady_clock, ns)
    uint32_t device_id;
    float temperature;
    float voltage;
    float current;
};
```

---

## 1. Requirements

* Linux (developed/tested on Ubuntu 24.04; uses `pthread`, `getrusage`, `getopt_long`)
* GCC >= 9 or Clang >= 10 (C++17)
* CMake >= 3.13

```bash
sudo apt update
sudo apt install -y build-essential cmake
```

## 2. Project layout

```
telemetry-engine/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── telemetry.hpp     Telemetry struct, clock, cpu_relax()
│   ├── processing.hpp    RNG, data generator, filter, fixed-point, DeviceStats
│   ├── queues.hpp        MutexQueue and LockFreeQueue (Vyukov MPMC)
│   ├── aggregators.hpp   LocalAgg, MutexAgg, AtomicAgg
│   ├── metrics.hpp       LatencyHistogram, CPU/RSS accounting
│   └── engine.hpp        producers + worker pool + reference implementation
├── src/main.cpp          CLI, reporting, verification
├── tests/test_core.cpp   unit + stress tests
└── scripts/
    ├── build.sh          build + run tests (release | native | tsan | asan)
    └── benchmark.sh      sweep workers x queue x aggregation -> CSV
```

## 3. Build

### Quick way

```bash
cd telemetry-engine
chmod +x scripts/*.sh        # only needed if permissions were lost (e.g. after unzip)
./scripts/build.sh
```

This configures a Release build in `./build`, compiles, and runs the test-suite.
You should end with `100% tests passed out of 7` and
`Built OK -> build/telemetry_engine`.

### Manual way

```bash
cd telemetry-engine
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
cd build && ctest --output-on-failure
```

### Build options

| Command | Effect |
|---|---|
| `./scripts/build.sh native` | adds `-march=native` (fastest on your own machine) |
| `./scripts/build.sh tsan` | ThreadSanitizer build in `./build-tsan` (detects data races) |
| `./scripts/build.sh asan` | AddressSanitizer + UBSan build in `./build-asan` |
| `cmake -S . -B build -DSANITIZE=thread` | same thing, manually |

> Benchmark with the plain **Release** (or `native`) build. Sanitizer builds are
> 5-20x slower and only meant for correctness checking.

## 4. Run

```bash
# defaults: 5M records, 2 producers, 4 workers, lock-free queue, local aggregation
./build/telemetry_engine

# same, but also prove the result is correct
./build/telemetry_engine --verify

# compare queue types
./build/telemetry_engine -q mutex    -n 10M -p 2 -w 4
./build/telemetry_engine -q lockfree -n 10M -p 2 -w 4

# compare aggregation strategies
./build/telemetry_engine -a local  -n 10M -w 8
./build/telemetry_engine -a mutex  -n 10M -w 8
./build/telemetry_engine -a atomic -n 10M -w 8

# scale the worker pool, pinned to cores
./build/telemetry_engine -w 8 --pin

# latency under a realistic fixed load: 2 producers x 200k rec/s = 400k rec/s
./build/telemetry_engine -r 200K -n 4M
```

### Command-line options

| Option | Default | Meaning |
|---|---|---|
| `-n, --records N` | 5000000 | total records generated (all producers combined) |
| `-p, --producers N` | 2 | producer threads |
| `-w, --workers N` | 4 | worker threads |
| `-c, --capacity N` | 65536 | queue capacity (lock-free rounds up to a power of 2) |
| `-q, --queue KIND` | lockfree | `mutex` or `lockfree` |
| `-a, --agg KIND` | local | `local`, `mutex` or `atomic` |
| `-d, --devices N` | 1024 | number of distinct device ids |
| `-s, --seed N` | 42 | RNG seed (same seed => same data) |
| `-r, --rate N` | 0 | records/s **per producer**; 0 = as fast as possible |
| `--pin` | off | pin producers/workers to CPU cores round-robin |
| `--verify` | off | recompute single-threaded and compare |
| `--csv` | off | print one CSV line instead of the report |
| `--csv-header` | off | print the CSV header (use with `--csv`) |
| `-h, --help` | | usage |

Numbers accept `K`/`M`/`G` suffixes (`-n 10M`, `-c 64K`). The program exits
with **0** on success and **1** if any integrity or verification check fails
(2 for bad arguments).

### Example output

```
=== Telemetry Processing Engine ===
Config
  queue / aggregation : lockfree / local
  producers / workers : 2 / 4
  records             : 2000000  (devices: 1024, queue capacity: 65536)
  producer rate       : unthrottled
  hardware threads    : 1

Throughput
  wall time           : 0.208 s
  throughput          : 9.61 M records/s

Latency (generation -> processed, includes queueing/backpressure)
  avg / p50           : 2465.94 / 2555.90 us
  p99 / p99.9         : 3604.48 / 3604.48 us
  max                 : 18241.26 us

Resources
  CPU time            : 0.208 s user + 0.000 s sys
  CPU utilisation     : 100% (100% = one fully busy core)
  peak RSS            : 5.9 MB

Results
  processed / rejected: 1980057 / 19943
  over-temp alarms    : 39860 (> 85 C)
  mean temperature    : 46.06 C
  mean power          : 4102.1 mW
  max temperature     : 110.00 C
  hottest devices     : #81(110.00C) #169(110.00C) #390(110.00C) #407(110.00C) #457(110.00C)
  integrity check     : OK (no loss)
  verify vs reference : PASS
```

(That sample was captured on a **1-core** sandbox, so absolute numbers and
scaling are not representative. Run it on your own multi-core machine.)

## 5. Benchmarking

```bash
./scripts/benchmark.sh
```

Sweeps `workers x {mutex,lockfree} queue x {local,mutex,atomic} aggregation`,
repeats each configuration, runs `--verify` on every run, and writes
`results/bench_<timestamp>.csv` plus a compact summary on screen.

Customise through environment variables:

```bash
RECORDS=20M PRODUCERS=4 WORKERS_LIST="1 2 4 8 16" REPEAT=5 PIN=1 ./scripts/benchmark.sh
```

CSV columns: `queue, agg, producers, workers, records, capacity, devices, rate,
wall_s, throughput_mrec_s, processed, rejected, alarms, lat_avg_us, lat_p50_us,
lat_p99_us, lat_p999_us, lat_max_us, cpu_pct, cpu_user_s, cpu_sys_s,
peak_rss_mb, verified`. Open it in a spreadsheet, pandas, or gnuplot.

Tips for trustworthy numbers:

* Close other heavy programs; use a Release or `native` build.
* On laptops set the governor to performance:
  `sudo cpupower frequency-set -g performance` (if installed).
* Use `PIN=1` and keep `producers + workers <= hardware threads`
  (oversubscription turns spin-waiting into scheduler noise).
* Take the median of several runs (`REPEAT=5`).
* Extra tooling: `perf stat -d ./build/telemetry_engine ...`, `htop`, `perf top`.

## 6. How it works

### Queues (`include/queues.hpp`)

Both queues are **bounded, multi-producer/multi-consumer**, with the same API
(`push`, `pop`, `close`). When full, producers block (backpressure); when empty,
workers block.

* **`MutexQueue`** - ring buffer guarded by one `std::mutex` with two condition
  variables (`not_full`, `not_empty`). Threads sleep in the kernel when they
  must wait, so it is CPU-friendly but pays for syscalls and lock hand-offs.
* **`LockFreeQueue`** - Dmitry Vyukov's bounded MPMC ring. Every cell has a
  sequence number; producers/consumers claim a slot with a single CAS on the
  head/tail counter and publish with a release store. Waiting threads spin
  (`pause`/`yield` instruction) and then `yield()`, so latency is lower and
  throughput higher, but waiting threads **burn CPU** - visible in the CPU%
  column.

### Clean shutdown

Producers are joined first, *then* the queue is closed. Workers drain whatever is
left and exit when `pop()` returns `false` (closed **and** empty). In the
lock-free queue the `closed` flag is read *before* the final `try_pop`, which
avoids losing the last items.

### Aggregation (`include/aggregators.hpp`)

| `--agg` | Hot path | Trade-off |
|---|---|---|
| `local` | each worker updates private per-device arrays; merged once at the end | no sharing, scales best; needs `workers x devices` memory |
| `mutex` | one global `std::mutex` around the stats table | simplest shared design; serialises all workers |
| `atomic` | `fetch_add` on per-device cache-line-aligned counters, CAS loop for max | no lock, but still cache-line ping-pong when workers hit the same device |

All sums are **fixed-point integers** (temperature x100, power in mW), so the
final numbers are identical no matter the interleaving. (Floating-point sums
would depend on summation order.)

### Latency measurement

`Telemetry::timestamp` is set by the producer right after generation;
the worker computes `now - timestamp` after processing. That is the
**end-to-end** time, **including time spent waiting for queue space**. Consequences:

* Unthrottled (`-r 0`), the queue is always full, so latency is roughly
  `queue_capacity / throughput` - it measures backpressure, not the queue's
  intrinsic delay. Use a smaller `-c` to see it drop.
* For meaningful per-record latency use `-r` to run below saturation
  (e.g. `-r 200K`).

Latencies are collected in per-worker log-linear histograms (16 sub-buckets per
power of two, ~3% resolution, no allocation on the hot path) and merged at the
end, so percentiles are approximate to a few percent; `max` is exact.

### Metrics

* **Throughput** = records / wall time (from "go" signal to last worker joined).
* **CPU utilisation** = (user + sys CPU time of the process) / wall time.
  100% = one fully busy core; 400% = four cores busy. Spinning threads count as busy.
* **Memory** = peak resident set size (`ru_maxrss`).

### Correctness checks built in

1. `processed + rejected == records` (nothing lost or duplicated).
2. Latency sample count equals record count.
3. Sum of per-device counts/alarms equals worker counters.
4. `--verify`: a single-threaded reference regenerates the same data (deterministic
   per-producer RNG streams) and every per-device statistic must match exactly.
5. `ctest`: queue stress tests (distinct ids, each must arrive exactly once, with
   capacity 1/7/1024 and 1-3 producers x 1-4 consumers), histogram tests,
   filter tests, aggregator-vs-aggregator tests under contention, and
   end-to-end `--verify` runs for all 6 queue x aggregation combinations.
6. The whole suite was also run clean under **ThreadSanitizer** and
   **AddressSanitizer + UBSan** (`./scripts/build.sh tsan` / `asan` to repeat).

### The fake data

Each record picks a random device (`--devices`) and: ~1% get a garbage
temperature of 999 C (sensor fault, **rejected** by the filter), ~2% are
overheating (86-110 C, counted as **alarms** > 85 C), the rest are 20-70 C;
voltage 3-5 V, current 0.05-2 A. The filter also rejects NaN/inf and
out-of-range values.

## 7. Ideas to extend it

* Per-worker SPSC queues (producer `i` -> worker `i`) to remove CAS contention
* Batched push/pop (amortise atomics over 32-64 records)
* Sharded mutex aggregation (lock striping by `device_id`)
* `alignas(64)` experiments / false-sharing demonstrations
* NUMA-aware placement on dual-socket machines
* Writing the per-device summary to CSV/JSON, or a time-windowed aggregation

## 8. Troubleshooting

| Problem | Fix |
|---|---|
| `cmake: command not found` | `sudo apt install cmake` |
| `Permission denied: ./scripts/build.sh` | `chmod +x scripts/*.sh` |
| CMake too old (< 3.13) | `pip install cmake` or use a newer Ubuntu |
| Lock-free numbers look worse than mutex | You probably have more threads than cores; reduce `-w`/`-p`, or use `--pin` |
| Latency looks huge | Expected when unthrottled - see "Latency measurement"; use `-r` |
| `FAIL` in verify | A real bug - please report the exact command line; run the `tsan` build |
