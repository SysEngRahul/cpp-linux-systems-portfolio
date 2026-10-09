// processing.hpp - deterministic data generator, validation/filtering,
// fixed-point conversion and the per-device statistics record.
//
// Everything that affects the *result* of the pipeline lives here and uses
// integer fixed-point math for aggregation, so the final statistics are
// bit-for-bit identical regardless of thread count, queue type or
// aggregation strategy. That is what makes --verify possible.
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include "telemetry.hpp"

// ---------------------------------------------------------------- RNG ----
class Rng {
public:
    explicit Rng(uint64_t seed) {
        // splitmix64 to scramble the seed, then xorshift64* for speed.
        uint64_t z = seed + 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        s_ = z ? z : 0x2545F4914F6CDD1DULL;
    }
    uint64_t next() {
        s_ ^= s_ >> 12;
        s_ ^= s_ << 25;
        s_ ^= s_ >> 27;
        return s_ * 0x2545F4914F6CDD1DULL;
    }
    // Uniform float in [0,1) using 24 random bits (exactly representable).
    float next_float() {
        return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f);
    }

private:
    uint64_t s_;
};

// ---------------------------------------------------------- Generator ----
// Produces fake sensor readings. ~2% overheating readings, ~1% faulty
// readings (garbage temperature) that the filter stage must reject.
class Generator {
public:
    Generator(uint64_t seed, unsigned producer_index, uint32_t devices)
        : rng_(seed * 0x9E3779B97F4A7C15ULL + producer_index + 1),
          devices_(devices) {}

    // timestamp is left 0; the caller stamps it (the stamp is not part of
    // the deterministic content).
    Telemetry next() {
        Telemetry t;
        t.timestamp = 0;
        t.device_id = static_cast<uint32_t>(rng_.next() % devices_);
        const float kind = rng_.next_float();
        const float r1 = rng_.next_float();
        if (kind < 0.01f) {
            t.temperature = 999.0f;                 // sensor fault
        } else if (kind < 0.03f) {
            t.temperature = 86.0f + 24.0f * r1;     // overheating 86..110
        } else {
            t.temperature = 20.0f + 50.0f * r1;     // normal 20..70
        }
        const float r2 = rng_.next_float();
        const float r3 = rng_.next_float();
        t.voltage = 3.0f + 2.0f * r2;               // 3..5 V
        t.current = 0.05f + 1.95f * r3;             // 0.05..2.0 A
        return t;
    }

private:
    Rng rng_;
    uint32_t devices_;
};

// -------------------------------------------------- Filter / transform ----
constexpr float kAlarmTempC = 85.0f;

struct Sample {            // a validated, fixed-point record
    uint32_t device;
    int32_t temp_x100;     // temperature in 1/100 deg C
    int64_t power_mw;      // voltage * current in milliwatts
    bool alarm;            // over-temperature
};

// Returns false if the record must be dropped (sensor fault / bad id).
inline bool evaluate(const Telemetry& t, uint32_t devices, Sample& s) {
    if (t.device_id >= devices) return false;
    if (!std::isfinite(t.temperature) || !std::isfinite(t.voltage) ||
        !std::isfinite(t.current))
        return false;
    if (t.temperature < -40.0f || t.temperature > 125.0f) return false;
    if (t.voltage < 0.0f || t.voltage > 30.0f) return false;
    if (t.current < 0.0f || t.current > 20.0f) return false;

    s.device = t.device_id;
    s.temp_x100 = static_cast<int32_t>(std::lroundf(t.temperature * 100.0f));
    s.power_mw = static_cast<int64_t>(std::lroundf(t.voltage * t.current * 1000.0f));
    s.alarm = t.temperature > kAlarmTempC;
    return true;
}

// ---------------------------------------------------------- Statistics ----
struct DeviceStats {
    uint64_t count = 0;
    int64_t sum_temp_x100 = 0;
    int32_t max_temp_x100 = std::numeric_limits<int32_t>::min();
    int64_t sum_power_mw = 0;
    uint64_t alarms = 0;

    bool operator==(const DeviceStats& o) const {
        return count == o.count && sum_temp_x100 == o.sum_temp_x100 &&
               max_temp_x100 == o.max_temp_x100 &&
               sum_power_mw == o.sum_power_mw && alarms == o.alarms;
    }
    bool operator!=(const DeviceStats& o) const { return !(*this == o); }
};

inline void accumulate(DeviceStats& d, const Sample& s) {
    ++d.count;
    d.sum_temp_x100 += s.temp_x100;
    if (s.temp_x100 > d.max_temp_x100) d.max_temp_x100 = s.temp_x100;
    d.sum_power_mw += s.power_mw;
    if (s.alarm) ++d.alarms;
}

inline void merge_into(DeviceStats& dst, const DeviceStats& src) {
    dst.count += src.count;
    dst.sum_temp_x100 += src.sum_temp_x100;
    if (src.max_temp_x100 > dst.max_temp_x100) dst.max_temp_x100 = src.max_temp_x100;
    dst.sum_power_mw += src.sum_power_mw;
    dst.alarms += src.alarms;
}
