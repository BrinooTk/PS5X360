#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
mkdir -p build/runtime-host
clang++-18 -std=c++20 -Wall -Wextra src/native_memory_bridge_contract.cpp \
  platform/ps5/native_memory_calls.cpp -o build/runtime-host/native-memory-bridge-contract
build/runtime-host/native-memory-bridge-contract
bash tools/build-runtime.sh ps5
python3 tools/native-stage.py --runtime
stage="$root/build/native-runtime-stage"
APP_ENABLE_EXCEPTIONS=1 \
APP_STATIC_ARCHIVES='.local/cpu/libxenia_cpu_runtime.a .local/cpu/libxenia_platform_memory.a .local/cpu/libxenia_base_runtime.a .local/cpu/libxenia_x64_backend.a .local/cpu/libxenia_ppc_frontend.a .local/cpu/libxenia_cpu_config.a .local/cpu/libxenia_cpu_compiler.a .local/cpu/libxenia_hir_values.a .local/cpu/libxenia_ppc_decoder.a .local/cpu/libxenia_capstone.a .local/cpp/libcpp.a .local/cpp/libcppabi.a .local/cpp/libunwind.a .local/cpp/libc_helpers.a' \
APP_INCLUDE_PATHS='include .deps/xenia/src .deps/xenia .deps/xenia/third_party/llvm/include' \
APP_EXTRA_ASSET_DIRS='roms' bash "$stage/tools/build.sh" Folder
"$stage/build/host/ps5-native-tool" self --extract --file "$stage/dist/PPSA50010/sce_module/libc.prx" \
  --out "$stage/build/libc.elf"
if llvm-nm-18 -u "$stage/build/llvm-pie.elf" | grep -E 'kernel_(mprotect|set_vmem_protection|copyin|copyout|get_proc)'; then
  echo 'Unexpected payload privilege helper in native title' >&2
  exit 1
fi
