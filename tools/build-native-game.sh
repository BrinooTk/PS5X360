#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
reference="$root/.deps/references/PS5_Vulkan"
sdk="$reference/.deps/native/ps5-payload-sdk"
work="$root/build/native-game"
mkdir -p "$work/obj" "$work/host"
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
link_native() {
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr --gc-sections "$@" \
  "${radv_link_flags[@]}" --wrap=mmap --wrap=mprotect --wrap=munmap --wrap=ffs \
  --version-script "$native/app-symbols.map" --version-script "$root/tooling/native-game-symbols.map" \
  --exclude-libs=ALL -e _start \
  -o "$work/llvm-pie.elf" "$work/obj/app_crt.o" \
  build/kernel-ps5/CMakeFiles/xenia-native-game-objects.dir/src/native_game_main.cpp.o \
  build/kernel-ps5/CMakeFiles/xenia-native-game-objects.dir/platform/ps5/native_memory_calls.cpp.o \
  build/kernel-ps5/CMakeFiles/xenia-native-game-objects.dir/platform/ps5/libc_bits.c.o \
  build/radv-stubs/libSceAgc.so build/radv-stubs/libSceAgcDriver.so \
  --start-group build/kernel-ps5/libxenia_*.a build/ffmpeg-ps5/libavcodec/libavcodec.a \
  build/ffmpeg-ps5/libavutil/libavutil.a --end-group \
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
app="$root/dist/PPSA50011"
mkdir -p "$app/sce_sys" "$app/sce_module" "$app/assets/roms"
"$tool" self --sign --in "$work/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp build/native-runtime-stage/dist/PPSA50010/sce_module/libc.prx "$app/sce_module/libc.prx"
cp build/native-runtime-stage/dist/PPSA50010/sce_sys/icon0.png "$app/sce_sys/icon0.png"
python3 tools/package-game.py --folder
"$tool" self --inspect --file "$app/eboot.bin" > "$work/fself-inspection.txt"
"$tool" self --inspect --file "$app/sce_module/libc.prx" > "$work/libc-inspection.txt"
"$tool" self --extract --file "$app/sce_module/libc.prx" --out "$work/libc.elf"
python3 tools/package-game.py
