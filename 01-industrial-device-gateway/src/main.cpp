#include "can/can_receiver.hpp"
#include "common/thread_safe_queue.hpp"
#include "protocol/protocol_decoder.hpp"

#include <iostream>
#include <string>

int main()
{
    try
    {
        gateway::CanReceiver receiver("vcan0");

        gateway::ProtocolDecoder decoder;

        gateway::ThreadSafeQueue<
            gateway::TelemetryMessage> message_queue;

        std::cout
            << "Industrial Device Gateway started\n";

        std::cout
            << "Listening on vcan0...\n";

        while (true)
        {
            auto frame = receiver.receive();

            auto message =
                decoder.decode(frame);

            if (!message)
            {
                std::cout
                    << "[Gateway] "
                    << "Unknown or invalid frame\n";

                continue;
            }

            message_queue.push(*message);

            auto decoded =
                message_queue.pop();

            std::cout
                << "[Gateway] "
                << "Device="
                << decoded.device_id
                << " Temperature="
                << decoded.value
                << " C"
                << " Sequence="
                << static_cast<int>(
                       decoded.sequence)
                << '\n';
        }
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "Gateway error: "
            << error.what()
            << '\n';

        return 1;
    }

    return 0;
}
