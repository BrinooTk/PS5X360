#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
bash tools/build.sh ps5
python3 tools/native-stage.py --cpu-translation
stage="$root/build/native-cpu-stage"
APP_ENABLE_EXCEPTIONS=1 \
APP_STATIC_ARCHIVES='.local/cpu/libxenia_ppc_frontend.a .local/cpu/libxenia_cpu_config.a .local/cpu/libxenia_cpu_compiler.a .local/cpu/libxenia_hir_values.a .local/cpu/libxenia_ppc_decoder.a .local/cpp/libcpp.a .local/cpp/libcppabi.a .local/cpp/libunwind.a .local/cpp/libc_helpers.a' \
APP_INCLUDE_PATHS='include .deps/xenia/src .deps/xenia .deps/xenia/third_party/llvm/include' \
APP_EXTRA_ASSET_DIRS='roms' bash "$stage/tools/build.sh" Folder
"$stage/build/host/ps5-native-tool" self --extract --file "$stage/dist/PPSA50009/sce_module/libc.prx" \
  --out "$stage/build/libc.elf"
python3 tools/package-native-probe.py --cpu-translation
