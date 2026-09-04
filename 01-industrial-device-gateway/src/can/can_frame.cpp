#include "can/can_frame.hpp"

#include <algorithm>

namespace gateway
{

CanFrame::CanFrame(uint32_t id,
                   uint8_t dlc,
                   const uint8_t* payload)
{
    frame_.can_id = id;
    frame_.can_dlc = dlc;

    std::copy(payload,
              payload + dlc,
              frame_.data);
}

uint32_t CanFrame::id() const noexcept
{
    return frame_.can_id;
}

uint8_t CanFrame::dlc() const noexcept
{
    return frame_.can_dlc;
}

const uint8_t* CanFrame::data() const noexcept
{
    return frame_.data;
}

const can_frame& CanFrame::native() const noexcept
{
    return frame_;
}

}
