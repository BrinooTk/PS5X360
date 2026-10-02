#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
checkout="$root/.deps/xenia/third_party/FFmpeg"
source_tree="$root/.deps/work/ffmpeg-src"
revision=15ece0882e8d5875051ff5b73c5a8326f7cee9f5
[[ $(git -C "$checkout" rev-parse HEAD) == "$revision" ]] || { echo 'FFmpeg revision mismatch' >&2; exit 1; }
if [[ $(cat "$source_tree/.revision" 2>/dev/null) != "$revision" ]]; then
  mkdir -p "$source_tree"
  git -C "$checkout" archive "$revision" | tar -x -C "$source_tree"
  printf '%s\n' "$revision" > "$source_tree/.revision"
fi
# Xenia ships a preconfigured Windows config in this fork. Remove only those
# files from our exported cache so configure can make independent target builds.
rm -f "$source_tree/config.h" "$source_tree/config.asm" "$source_tree/config.mak"
mode=${1:-host}
build="$root/build/ffmpeg-$mode"
mkdir -p "$build"
cd "$build"
options=(--disable-everything --disable-programs --disable-doc --disable-network
  --disable-avdevice --disable-avfilter --disable-swresample --disable-swscale
  --disable-avformat --disable-x86asm --disable-shared --enable-static
  --disable-autodetect --enable-pic --enable-decoder=xma1,xma2,xmaframes --enable-parser=xma
  '--extra-cflags=-O3 -march=znver2 -fno-stack-protector')
case "$mode" in
 host) options+=(--cc=clang-18 --cxx=clang++-18 --ar=llvm-ar-18 --nm=llvm-nm-18 --ranlib=llvm-ranlib-18) ;;
 ps5)
   : "${PS5_PAYLOAD_SDK:?Set PS5_PAYLOAD_SDK}"
   options+=(--enable-cross-compile --arch=x86_64 --target-os=freebsd
     "--cc=$PS5_PAYLOAD_SDK/bin/prospero-clang" "--cxx=$PS5_PAYLOAD_SDK/bin/prospero-clang++"
     "--ar=$PS5_PAYLOAD_SDK/bin/prospero-ar" "--nm=$PS5_PAYLOAD_SDK/bin/prospero-nm"
     "--ranlib=$PS5_PAYLOAD_SDK/bin/prospero-ranlib" "--sysroot=$PS5_PAYLOAD_SDK/target") ;;
 *) echo 'Usage: build-audio-codecs.sh host|ps5' >&2; exit 2 ;;
esac
"$source_tree/configure" "${options[@]}"
make -j4 libavcodec/libavcodec.a libavutil/libavutil.a
printf '%s\n' "$revision" > codec-revision.txt
printf '%s\n' 'xma1,xma2,xmaframes-v2' > codec-profile.txt
sha256sum libavcodec/libavcodec.a libavutil/libavutil.a > codec-sha256.txt
