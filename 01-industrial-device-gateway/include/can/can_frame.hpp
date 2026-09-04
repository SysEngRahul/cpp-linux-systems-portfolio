#pragma once

#include <linux/can.h>
#include <cstdint>

namespace gateway
{

class CanFrame
{
public:

    CanFrame() = default;

    CanFrame(uint32_t id,
             uint8_t dlc,
             const uint8_t* payload);

    uint32_t id() const noexcept;

    uint8_t dlc() const noexcept;

    const uint8_t* data() const noexcept;

    const can_frame& native() const noexcept;

private:

    can_frame frame_ {};
};

}
