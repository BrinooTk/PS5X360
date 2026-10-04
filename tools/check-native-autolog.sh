#!/usr/bin/env bash
set -euo pipefail
out="$PWD/build/autolog-native"
source_dir="$PWD/.deps/autolog/mbedtls-3.6.7"
mkdir -p "$out/host-obj"
export out source_dir
export AUTOLOG_INCLUDES="$PWD/native/autolog"
export AUTOLOG_HOST_CC="${CC:-clang-18}"
command -v "$AUTOLOG_HOST_CC" >/dev/null
find "$source_dir/library" -maxdepth 1 -name '*.c' -print0 | xargs -0 -P 4 -I{} bash -c '
  name="$(basename "$1" .c)"
  "$AUTOLOG_HOST_CC" -O1 -g -fsanitize=address,undefined -DMBEDTLS_USER_CONFIG_FILE=\"mbedtls_user_config.h\" -I"$AUTOLOG_INCLUDES" -I"$source_dir/include" -c "$1" -o "$out/host-obj/$name.o"
' _ {}
"$AUTOLOG_HOST_CC" -O1 -g -fsanitize=address,undefined -D'MBEDTLS_USER_CONFIG_FILE="mbedtls_user_config.h"' -I"$AUTOLOG_INCLUDES" -I"$out" -I"$source_dir/include" native/autolog/test.c "$out"/host-obj/*.o -o "$out/native-tests"
"$out/native-tests" "${1:-}"
