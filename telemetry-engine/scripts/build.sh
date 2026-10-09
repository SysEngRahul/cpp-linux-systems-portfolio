#!/usr/bin/env bash
# Configure + build (Release) into ./build, then run the test-suite.
#   ./scripts/build.sh            normal build
#   ./scripts/build.sh native     add -march=native
#   ./scripts/build.sh tsan       ThreadSanitizer build into ./build-tsan
set -euo pipefail
cd "$(dirname "$0")/.."

mode="${1:-release}"
case "$mode" in
  release) dir=build;      args=(-DCMAKE_BUILD_TYPE=Release) ;;
  native)  dir=build;      args=(-DCMAKE_BUILD_TYPE=Release -DNATIVE=ON) ;;
  tsan)    dir=build-tsan; args=(-DCMAKE_BUILD_TYPE=RelWithDebInfo -DSANITIZE=thread) ;;
  asan)    dir=build-asan; args=(-DCMAKE_BUILD_TYPE=RelWithDebInfo "-DSANITIZE=address,undefined") ;;
  *) echo "usage: $0 [release|native|tsan|asan]"; exit 2 ;;
esac

cmake -S . -B "$dir" "${args[@]}"
cmake --build "$dir" -j"$(nproc)"
(cd "$dir" && ctest --output-on-failure)
echo
echo "Built OK -> $dir/telemetry_engine"
