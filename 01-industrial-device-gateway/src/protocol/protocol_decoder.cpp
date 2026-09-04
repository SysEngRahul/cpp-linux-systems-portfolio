#include "protocol/protocol_decoder.hpp"

namespace gateway
{

namespace
{

constexpr uint32_t TEMPERATURE_ID = 0x101;

}

std::optional<TelemetryMessage>
ProtocolDecoder::decode(const CanFrame& frame) const
{
    if (frame.id() != TEMPERATURE_ID)
    {
        return std::nullopt;
    }

    if (frame.dlc() < 4)
    {
        return std::nullopt;
    }

    const uint8_t* data = frame.data();

    uint16_t raw_temperature =
        static_cast<uint16_t>(data[0]) |
        (static_cast<uint16_t>(data[1]) << 8);

    uint32_t device_id = data[2];

    uint8_t sequence = data[3];

    TelemetryMessage message;

    message.device_id = device_id;
    message.type = MessageType::Temperature;
    message.value =
        static_cast<double>(raw_temperature) / 10.0;

    message.sequence = sequence;
    message.timestamp =
        std::chrono::system_clock::now();

    return message;
}

}
