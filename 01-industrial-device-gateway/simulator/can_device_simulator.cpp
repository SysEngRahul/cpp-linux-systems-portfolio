#include <linux/can.h>
#include <linux/can/raw.h>

#include <net/if.h>

#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <thread>

int main()
{
    int socket_fd =
        socket(
            PF_CAN,
            SOCK_RAW,
            CAN_RAW);

    if (socket_fd < 0)
    {
        throw std::runtime_error(
            "Failed to create CAN socket");
    }

    ifreq ifr {};

    std::strncpy(
        ifr.ifr_name,
        "vcan0",
        IFNAMSIZ - 1);

    if (ioctl(
            socket_fd,
            SIOCGIFINDEX,
            &ifr) < 0)
    {
        close(socket_fd);

        throw std::runtime_error(
            "Failed to find vcan0");
    }

    sockaddr_can address {};

    address.can_family = AF_CAN;
    address.can_ifindex = ifr.ifr_ifindex;

    if (bind(
            socket_fd,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0)
    {
        close(socket_fd);

        throw std::runtime_error(
            "Failed to bind CAN socket");
    }

    std::mt19937 generator(
        std::random_device{}());

    std::uniform_int_distribution<int>
        temperature(200, 950);

    uint8_t sequence = 0;

    while (true)
    {
        can_frame frame {};

        frame.can_id = 0x101;
        frame.can_dlc = 4;

        const uint16_t temp =
            temperature(generator);

        frame.data[0] =
            static_cast<uint8_t>(temp & 0xFF);

        frame.data[1] =
            static_cast<uint8_t>(
                (temp >> 8) & 0xFF);

        frame.data[2] = 12;
        frame.data[3] = sequence++;

        const ssize_t bytes =
            write(
                socket_fd,
                &frame,
                sizeof(frame));

        if (bytes != sizeof(frame))
        {
            std::cerr
                << "Failed to transmit CAN frame\n";
        }
        else
        {
            std::cout
                << "[Simulator] "
                << "Temperature="
                << temp / 10.0
                << " C"
                << " Sequence="
                << static_cast<int>(
                       frame.data[3])
                << '\n';
        }

        std::this_thread::sleep_for(
            std::chrono::seconds(1));
    }

    close(socket_fd);

    return 0;
}
