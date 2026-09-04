#pragma once

#include <cstdint>
#include <chrono>

namespace gateway
{

enum class MessageType
{
    Heartbeat,
    Temperature,
    Voltage,
    Current,
    Status,
    Unknown
};

struct TelemetryMessage
{
    uint32_t device_id {};
    MessageType type {MessageType::Unknown};

    double value {};
    uint8_t sequence {};

    std::chrono::system_clock::time_point timestamp {};
};

}
