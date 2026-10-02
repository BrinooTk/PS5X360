#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
python3 tools/prepare.py
case "${1:-host}" in
  host)
    cmake -S . -B build/runtime-host -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
      -DXBOX360PS5_BUILD_RUNTIME=ON
    cmake --build build/runtime-host -j 6
    ctest --test-dir build/runtime-host --output-on-failure --test-output-size-passed 65536 --output-junit runtime-tests.xml
    python3 -m unittest discover -s tools -p 'test_*.py'
    ;;
  ps5)
    : "${PS5_PAYLOAD_SDK:?Set PS5_PAYLOAD_SDK to the existing public SDK}"
    cmake -S . -B build/runtime-ps5 -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
      -DCMAKE_CXX_FLAGS='-O3 -march=znver2 -fno-stack-protector' \
      -DXBOX360PS5_BUILD_RUNTIME=ON
    cmake --build build/runtime-ps5 -j 6
    python3 tools/verify.py xenia-memory-contract --build-dir build/runtime-ps5
    python3 tools/verify.py xenia-runtime-link --build-dir build/runtime-ps5
    ;;
  *) echo 'usage: tools/build-runtime.sh [host|ps5]' >&2; exit 2 ;;
esac
