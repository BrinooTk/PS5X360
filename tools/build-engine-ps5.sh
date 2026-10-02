#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
: "${PS5_PAYLOAD_SDK:?Set PS5_PAYLOAD_SDK}"
reference="$root/.deps/references/PS5_Vulkan"
radv_sdk="$reference/.deps/native/ps5-payload-sdk"
mkdir -p build
mkdir -p build/radv-stubs
# These libraries are metadata for imports from the console's real modules.
# They are never copied into the application or used as runtime GPU emulation.
for pair in 'libSceAgc:agc_canary_link_stub.c' 'libSceAgcDriver:agc_driver_canary_link_stub.c'; do
  library=${pair%%:*}
  file=${pair#*:}
  "$radv_sdk/bin/prospero-clang" -O2 -fPIC -c "$reference/vendor/ps5/sdk/stubs/$file" -o "build/radv-stubs/$library.o"
  "$radv_sdk/bin/prospero-lld" --shared -soname "$library.prx" \
    -o "build/radv-stubs/$library.so" "build/radv-stubs/$library.o"
done
bash tools/build-integration.sh ps5
cmake --build build/kernel-ps5 --target xenia-native-game-objects -j4
bash tools/build-native-game.sh
