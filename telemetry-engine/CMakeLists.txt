cmake_minimum_required(VERSION 3.13)
project(telemetry_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Optional: -DSANITIZE=thread | address | undefined
set(SANITIZE "" CACHE STRING "Sanitizer to enable (thread, address, undefined)")
# Optional: -DNATIVE=ON to add -march=native
option(NATIVE "Compile with -march=native" OFF)

find_package(Threads REQUIRED)

add_library(telemetry_core INTERFACE)
target_include_directories(telemetry_core INTERFACE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(telemetry_core INTERFACE Threads::Threads)
# -ffp-contract=off: keep float results identical between the threaded run
# and the single-threaded --verify reference (no FMA surprises on ARM/x86).
target_compile_options(telemetry_core INTERFACE -Wall -Wextra -Wpedantic -ffp-contract=off)

if(NATIVE)
  target_compile_options(telemetry_core INTERFACE -march=native)
endif()
if(SANITIZE)
  target_compile_options(telemetry_core INTERFACE -fsanitize=${SANITIZE} -fno-omit-frame-pointer -g)
  target_link_options(telemetry_core INTERFACE -fsanitize=${SANITIZE})
endif()

add_executable(telemetry_engine src/main.cpp)
target_link_libraries(telemetry_engine PRIVATE telemetry_core)

add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE telemetry_core)

enable_testing()
add_test(NAME unit_tests COMMAND test_core)
foreach(Q mutex lockfree)
  foreach(A local mutex atomic)
    add_test(NAME e2e_${Q}_${A}
      COMMAND telemetry_engine -n 300000 -p 3 -w 4 -c 1024 -q ${Q} -a ${A} -d 257 --verify)
  endforeach()
endforeach()
