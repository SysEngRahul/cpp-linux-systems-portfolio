// telemetry.hpp - core record type, clock helper, CPU hint.
#pragma once

#include <chrono>
#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

struct Telemetry {
    uint64_t timestamp;   // creation time, steady_clock nanoseconds
    uint32_t device_id;
    float temperature;    // deg C
    float voltage;        // V
    float current;        // A
};
static_assert(sizeof(Telemetry) == 24, "unexpected Telemetry layout");

// Monotonic, system-wide clock: comparable across threads.
inline uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// Politely tell the CPU we are in a spin-wait loop.
inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
    asm volatile("yield" ::: "memory");
#else
    // no-op
#endif
}
