#!/usr/bin/env bash
# Builds the PS5 title on the Xenia Canary core.
#   PS5_PAYLOAD_SDK=<sdk> bash tools/build-canary-game.sh
# Canary's libraries and the title's objects are compiled by CMake
# (canary/CMakeLists.txt, with the SDK's toolchain); the executable is linked
# here with the native title start-up code and the RADV driver, as
# tools/build-native-game.sh does for the original core.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
: "${PS5_PAYLOAD_SDK:?Set PS5_PAYLOAD_SDK}"
reference="$root/.deps/references/PS5_Vulkan"
sdk="$reference/.deps/native/ps5-payload-sdk"
build="$root/build/canary-ps5"
work="$root/build/canary-game"
mkdir -p "$work/obj" "$work/host" build/radv-stubs

# Import metadata for the console's own graphics modules (never shipped).
for pair in 'libSceAgc:agc_canary_link_stub.c' 'libSceAgcDriver:agc_driver_canary_link_stub.c'; do
  library=${pair%%:*}
  file=${pair#*:}
  "$sdk/bin/prospero-clang" -O2 -fPIC -c "$reference/vendor/ps5/sdk/stubs/$file" -o "build/radv-stubs/$library.o"
  "$sdk/bin/prospero-lld" --shared -soname "$library.prx" -o "build/radv-stubs/$library.so" "build/radv-stubs/$library.o"
done

if [[ ! -f "$build/build.ninja" ]]; then
  cmake -S canary -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake"
fi
# Canary's xenia-build.py normally writes this.
commit=$(git -C .deps/xenia-canary rev-parse HEAD)
cat > "$build/version.h.new" <<EOF
#ifndef GENERATED_VERSION_H_
#define GENERATED_VERSION_H_
#define XE_BUILD_BRANCH "canary_experimental"
#define XE_BUILD_COMMIT "$commit"
#define XE_BUILD_COMMIT_SHORT "${commit:0:7}"
#define XE_BUILD_DATE __DATE__
#endif  // GENERATED_VERSION_H_
EOF
cmp -s "$build/version.h.new" "$build/version.h" || mv "$build/version.h.new" "$build/version.h"
ninja -C "$build" -j"${JOBS:-8}" xbox360ps5-title-objects \
  xenia-base xenia-cpu xenia-cpu-backend-x64 xenia-core xenia-kernel xenia-gpu xenia-gpu-vulkan \
  xenia-ui xenia-ui-vulkan xenia-vfs xenia-apu xenia-apu-nop xenia-hid xenia-patcher \
  aes_128 capstone fmt dxbc imgui libavcodec libavutil mspack snappy xxhash glslang-spirv

native="$reference/tooling/native"
zlib="$reference/.deps/native/zlib/root/usr"
clang++-18 -std=c++20 -O2 -I "$zlib/include" \
  "$native/native_app_builder.cpp" "$native/self_container.cpp" \
  "$native/elf_object.cpp" "$native/sce_module_writer.cpp" \
  "$zlib/lib/libz.a" -o "$work/host/ps5-native-tool"
PS5_PAYLOAD_SDK="$sdk" PS5_CLANG=clang-18 sh "$reference/tooling/prospero-clang18" \
  -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
  -c "$native/app_crt.cpp" -o "$work/obj/app_crt.o"
source "$reference/tools/radv-link.sh"
PS5_CLANG=clang-18 radv_link_recipe "$reference" "$sdk" \
  "$reference/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
mapfile -t objects < <(find "$build/CMakeFiles/xbox360ps5-title-objects.dir" -name '*.o' | sort)
mapfile -t archives < <(find "$build" -name '*.a' | sort)
link_native() {
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr --gc-sections "$@" \
  "${radv_link_flags[@]}" --wrap=mmap --wrap=mprotect --wrap=munmap --wrap=ffs --wrap=stat --wrap=lstat --wrap=timegm \
  --version-script "$native/app-symbols.map" --version-script "$root/tooling/native-game-symbols.map" \
  --exclude-libs=ALL -e _start \
  -o "$work/llvm-pie.elf" "$work/obj/app_crt.o" "${objects[@]}" \
  build/radv-stubs/libSceAgc.so build/radv-stubs/libSceAgcDriver.so \
  --start-group "${archives[@]}" --end-group \
  "${radv_link_inputs[@]}" --as-needed "$sdk"/target/lib/*.so
}
link_native
python3 tools/resolve-mesa-weaks.py "$work/llvm-pie.elf" "$work/mesa-weaks"
link_native @"$work/mesa-weaks.rsp" --version-script "$work/mesa-weaks.map"
if llvm-nm-18 "$work/llvm-pie.elf" | grep -E 'kernel_(mprotect|set_vmem_protection|copyin|copyout|get_proc)'; then
  echo 'Unexpected privilege helper dependency' >&2; exit 1
fi
tool="$work/host/ps5-native-tool"
"$tool" link --in "$work/llvm-pie.elf" --out "$work/eboot.elf" \
  --stub-dir "$sdk/target/lib" --stub build/radv-stubs/libSceAgc.so \
  --stub build/radv-stubs/libSceAgcDriver.so --module-sdk 0x02000009 \
  --companion-sdk 0x08050001 --file-name eboot.elf
python3 build/native-runtime-stage/tools/verify-image.py "$work/llvm-pie.elf" "$work/eboot.elf"
"$tool" self --sign --in "$work/eboot.elf" --out "$work/eboot.bin" --magic 0x1D3D154F
ls -la "$work/eboot.bin"
mkdir -p "$root/dist/PPSA50011/assets/fonts"
cp "$root/assets/fonts/"* "$root/dist/PPSA50011/assets/fonts/"
