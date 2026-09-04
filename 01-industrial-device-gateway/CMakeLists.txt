cmake_minimum_required(VERSION 3.16)

project(
    industrial_device_gateway
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
)

include_directories(include)

add_executable(
    gateway

    src/main.cpp

    src/can/can_frame.cpp
    src/can/can_receiver.cpp

    src/protocol/protocol_decoder.cpp
)

add_executable(
    can_device_simulator

    simulator/can_device_simulator.cpp
)

target_link_libraries(
    gateway
    pthread
)

target_link_libraries(
    can_device_simulator
    pthread
)
