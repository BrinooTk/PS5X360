#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
mode=${1:-host}
python3 tools/prepare.py
codec="build/ffmpeg-$mode"
if [[ ! -f "$codec/codec-revision.txt" || $(cat "$codec/codec-revision.txt") != 15ece0882e8d5875051ff5b73c5a8326f7cee9f5 ||
      ! -f "$codec/codec-profile.txt" || $(cat "$codec/codec-profile.txt") != xma1,xma2,xmaframes-v2 ]] ||
   ! (cd "$codec" && sha256sum -c codec-sha256.txt >/dev/null 2>&1); then
  bash tools/build-audio-codecs.sh "$mode"
fi
options=(-DCMAKE_BUILD_TYPE=Release -DXBOX360PS5_BUILD_RUNTIME=ON
  -DXBOX360PS5_BUILD_KERNEL=ON -DXBOX360PS5_BUILD_XENOS=ON -DXBOX360PS5_BUILD_AUDIO=ON)
case "$mode" in
 host)
  cmake -S . -B build/kernel-host -G Ninja "${options[@]}" -DCMAKE_C_COMPILER=clang-18 \
    -DCMAKE_CXX_COMPILER=clang++-18 -DXBOX360PS5_BUILD_VULKAN_PLATFORM=ON
  cmake --build build/kernel-host -j4
  ctest --test-dir build/kernel-host --output-on-failure --output-junit integration-tests.xml
  build/kernel-host/vulkan-transfer-probe > build/integration-vulkan-transfer.txt
  python3 -m unittest discover -s tools -p 'test_*.py'
  ;;
 ps5)
  : "${PS5_PAYLOAD_SDK:?Set PS5_PAYLOAD_SDK}"
  cmake -S . -B build/kernel-ps5 -G Ninja "${options[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
    -DCMAKE_CXX_FLAGS='-O3 -march=znver2 -fno-stack-protector'
  # The native coordinator/renderer are not a linked game application yet.
  cmake --build build/kernel-ps5 -j4 --target xenia_guest_kernel xenia_emulator_coordinator \
    xenia_dualsense_input xenia_audio_engine xenia_spirv_builder xenia_snappy xenia_xxhash \
    xenia_xenos_gpu xenia_vulkan_renderer xenia_vulkan_ui xenia-thread-contract \
    xenia-vfs-contract xenia-runtime-link xenia-input-contract xenia-audio-codec-contract
  for artifact in xenia-thread-contract xenia-vfs-contract xenia-runtime-link xenia-input-contract xenia-audio-codec-contract; do
    python3 tools/verify.py "$artifact" --build-dir build/kernel-ps5
  done
  ;;
 *) echo 'usage: build-integration.sh host|ps5' >&2; exit 2 ;;
esac
