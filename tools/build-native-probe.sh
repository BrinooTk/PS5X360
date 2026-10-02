#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
bash tools/build.sh ps5
python3 tools/native-stage.py
stage="$root/build/native-stage"
APP_STATIC_ARCHIVES='.local/decoder/libxenia_ppc_decoder.a' \
APP_INCLUDE_PATHS='include .deps/xenia/src .deps/xenia' \
APP_EXTRA_ASSET_DIRS='roms' bash "$stage/tools/build.sh" Folder
"$stage/build/host/ps5-native-tool" self --extract --file "$stage/dist/PPSA50008/sce_module/libc.prx" \
    --out "$stage/build/libc.elf"
python3 tools/package-native-probe.py
