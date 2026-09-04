#pragma once

#include "can/can_frame.hpp"
#include "protocol/message.hpp"

#include <optional>

namespace gateway
{

class ProtocolDecoder
{
public:

    std::optional<TelemetryMessage>
    decode(const CanFrame& frame) const;
};

}
