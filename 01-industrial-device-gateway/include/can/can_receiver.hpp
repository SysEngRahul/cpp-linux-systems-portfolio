#pragma once

#include "can/can_frame.hpp"

#include <string>

namespace gateway
{

class CanReceiver
{
public:

    explicit CanReceiver(const std::string& interface);

    ~CanReceiver();

    CanReceiver(const CanReceiver&) = delete;
    CanReceiver& operator=(const CanReceiver&) = delete;

    CanFrame receive();

private:

    int socket_fd_ {-1};
};

}
