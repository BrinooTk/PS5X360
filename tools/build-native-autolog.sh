#!/usr/bin/env bash
set -euo pipefail
sdk="${PS5_PAYLOAD_SDK:-$PWD/.deps/references/PS5_Vulkan/.deps/native/ps5-payload-sdk}"
source_dir="$PWD/.deps/autolog/mbedtls-3.6.7"
out="$PWD/build/autolog-native"
mkdir -p "$out/obj"
cc="$sdk/bin/prospero-clang"
flags=(-O2 -fPIC -DPS5 -D'MBEDTLS_USER_CONFIG_FILE="mbedtls_user_config.h"' -I"$PWD/native/autolog" -I"$out" -I"$source_dir/include")
export cc source_dir out
export AUTOLOG_INCLUDES="$PWD/native/autolog"
find "$source_dir/library" -maxdepth 1 -name '*.c' -print0 | xargs -0 -P "${JOBS:-4}" -I{} bash -c '
  name="$(basename "$1" .c)"
  "$cc" -O2 -fPIC -DPS5 -DMBEDTLS_USER_CONFIG_FILE=\"mbedtls_user_config.h\" -I"$AUTOLOG_INCLUDES" -I"$source_dir/include" -c "$1" -o "$out/obj/$name.o"
' _ {}
"$sdk/bin/prospero-ar" rcs "$out/libmbedtls.a" "$out"/obj/*.o
# Resolve imports with a small userland startup, without the SDK payload CRT.
"$cc" "${flags[@]}" -ffreestanding -fno-builtin -c "$PWD/native/autolog/start.c" -o "$out/start.o"
runtime="$PWD/.deps/references/PS5_PayloadSDK/crt"
if [[ ! -f "$out/sdk-syscall.c" ]]; then
  cp "$runtime/syscall.c" "$out/sdk-syscall.c"
  cp "$runtime/payload.h" "$out/payload.h"
  cp "$PWD/.deps/references/PS5_PayloadSDK/LICENSE" "$out/SDK-COPYING"
fi
"$cc" "${flags[@]}" -I"$out" -c "$out/sdk-syscall.c" -o "$out/syscall.o"
"$cc" "${flags[@]}" -nostartfiles -Wl,-e,autolog_start "$out/start.o" "$out/syscall.o" "$PWD/native/autolog/collector.c" "$out/libmbedtls.a" -o "$out/PS5X360-AutoLog.elf"
echo "Built $out/PS5X360-AutoLog.elf"
