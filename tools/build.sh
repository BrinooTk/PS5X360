#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
mode=${1:-host}
python3 tools/prepare.py
case "$mode" in
  host)
    cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-18
    cmake --build build/host
    ctest --test-dir build/host --output-on-failure
    ;;
  ps5)
    : "${PS5_PAYLOAD_SDK:?Set PS5_PAYLOAD_SDK to the existing public SDK}"
    cmake -S . -B build/ps5 -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
      -DCMAKE_CXX_FLAGS='-O3 -march=znver2 -fno-stack-protector'
    cmake --build build/ps5
    python3 tools/verify.py
    python3 tools/verify.py xenia-hir-smoke
    ;;
  *) echo 'usage: tools/build.sh [host|ps5]' >&2; exit 2 ;;
esac
