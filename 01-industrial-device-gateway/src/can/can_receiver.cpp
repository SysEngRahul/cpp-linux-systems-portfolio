#include "can/can_receiver.hpp"

#include <linux/can/raw.h>
#include <net/if.h>

#include <cstring>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace gateway
{

CanReceiver::CanReceiver(const std::string& interface)
{
    socket_fd_ = socket(
        PF_CAN,
        SOCK_RAW,
        CAN_RAW);

    if (socket_fd_ < 0)
    {
        throw std::runtime_error(
            "Failed to create CAN socket");
    }

    ifreq ifr {};

    std::strncpy(
        ifr.ifr_name,
        interface.c_str(),
        IFNAMSIZ - 1);

    if (ioctl(
            socket_fd_,
            SIOCGIFINDEX,
            &ifr) < 0)
    {
        close(socket_fd_);

        throw std::runtime_error(
            "Failed to obtain CAN interface index");
    }

    sockaddr_can address {};

    address.can_family = AF_CAN;
    address.can_ifindex = ifr.ifr_ifindex;

    if (bind(
            socket_fd_,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0)
    {
        close(socket_fd_);

        throw std::runtime_error(
            "Failed to bind CAN socket");
    }
}

CanReceiver::~CanReceiver()
{
    if (socket_fd_ >= 0)
    {
        close(socket_fd_);
    }
}

CanFrame CanReceiver::receive()
{
    can_frame frame {};

    const ssize_t bytes =
        read(
            socket_fd_,
            &frame,
            sizeof(frame));

    if (bytes != sizeof(frame))
    {
        throw std::runtime_error(
            "Invalid CAN frame received");
    }

    return CanFrame(
        frame.can_id,
        frame.can_dlc,
        frame.data);
}

}
