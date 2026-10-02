#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
python3 tools/prepare.py
case "${1:-host}" in
  host)
    cmake -S . -B build/vulkan-host -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
      -DXBOX360PS5_BUILD_RUNTIME=ON -DXBOX360PS5_BUILD_VULKAN_PLATFORM=ON
    cmake --build build/vulkan-host -j 6
    ctest --test-dir build/vulkan-host --output-on-failure --test-output-size-passed 65536 --output-junit vulkan-tests.xml
    build/vulkan-host/vulkan-transfer-probe
    python3 -m unittest discover -s tools -p 'test_*.py'
    ;;
  ps5)
    : "${PS5_PAYLOAD_SDK:?Set PS5_PAYLOAD_SDK to the existing public SDK}"
    cmake -S . -B build/vulkan-ps5 -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
      -DCMAKE_CXX_FLAGS='-O3 -march=znver2 -fno-stack-protector' \
      -DXBOX360PS5_BUILD_VULKAN_PLATFORM=ON
    cmake --build build/vulkan-ps5 -j 6 --target xbox360ps5_vulkan_platform vulkan-platform-contract
    python3 tools/verify.py vulkan-platform-contract --build-dir build/vulkan-ps5
    ;;
  *) echo 'usage: tools/build-vulkan-platform.sh [host|ps5]' >&2; exit 2 ;;
esac
