#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# The host tools Xenia Canary's build compiles its Vulkan shaders with
# (tools/build/compile_shader_spirv.py: glslangValidator, spirv-opt, spirv-dis,
# found in $VULKAN_SDK/bin), built from Canary's own submodules at their pins:
# its spirv-opt calls need flags (--canonicalize-ids) newer than a distribution's
# SPIRV-Tools. SPIRV-Headers comes at the revision SPIRV-Tools' DEPS names.
#
#   .github/scripts/build-shader-tools.sh <xenia-canary checkout> <output>
#   export VULKAN_SDK=<output>
set -euo pipefail
canary=$(cd -- "${1:?usage: build-shader-tools.sh <xenia-canary> <output>}" && pwd)
mkdir -p "${2:?usage: build-shader-tools.sh <xenia-canary> <output>}"
out=$(cd -- "$2" && pwd)
tools=$canary/third_party/SPIRV-Tools
glslang=$canary/third_party/glslang
[[ -f $tools/CMakeLists.txt && -f $glslang/CMakeLists.txt ]] ||
    { echo "build-shader-tools: $canary has no third_party/SPIRV-Tools or third_party/glslang" >&2; exit 2; }
stamp="$(git -C "$tools" rev-parse HEAD) $(git -C "$glslang" rev-parse HEAD)"
if [[ -x $out/bin/spirv-opt && -x $out/bin/spirv-dis && -x $out/bin/glslangValidator &&
      $(cat "$out/.revisions" 2>/dev/null) == "$stamp" ]]; then
    echo "==> [shader-tools] $out/bin is SPIRV-Tools and glslang at $stamp"
    exit 0
fi
headers_revision=$(sed -n "s/^ *'spirv_headers_revision': *'\([0-9a-f]\{40\}\)'.*/\1/p" "$tools/DEPS")
[[ -n $headers_revision ]] || { echo "build-shader-tools: no spirv_headers_revision in $tools/DEPS" >&2; exit 2; }
work=$out/work
headers=$work/SPIRV-Headers
if [[ $(git -C "$headers" rev-parse HEAD 2>/dev/null) != "$headers_revision" ]]; then
    rm -rf "$headers"
    git init -q "$headers"
    git -C "$headers" remote add origin https://github.com/KhronosGroup/SPIRV-Headers.git
    git -C "$headers" fetch -q --depth 1 origin "$headers_revision"
    git -C "$headers" checkout -q --detach FETCH_HEAD
fi
jobs=${JOBS:-$(nproc)}
cmake -S "$tools" -B "$work/spirv-tools" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSPIRV-Headers_SOURCE_DIR="$headers" -DSPIRV_SKIP_TESTS=ON -DSPIRV_WERROR=OFF \
    -DSPIRV_TOOLS_BUILD_STATIC=ON >"$work/spirv-tools.configure.log" ||
    { tail -30 "$work/spirv-tools.configure.log" >&2; exit 1; }
ninja -C "$work/spirv-tools" -j "$jobs" spirv-opt spirv-dis >"$work/spirv-tools.build.log" ||
    { grep -m20 -B2 -A8 error "$work/spirv-tools.build.log" >&2; exit 1; }
cmake -S "$glslang" -B "$work/glslang" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_OPT=OFF -DENABLE_HLSL=OFF -DGLSLANG_TESTS=OFF -DBUILD_EXTERNAL=OFF \
    -DENABLE_GLSLANG_BINARIES=ON >"$work/glslang.configure.log" ||
    { tail -30 "$work/glslang.configure.log" >&2; exit 1; }
ninja -C "$work/glslang" -j "$jobs" glslang-standalone >"$work/glslang.build.log" ||
    { grep -m20 -B2 -A8 error "$work/glslang.build.log" >&2; exit 1; }
mkdir -p "$out/bin"
cp "$work/spirv-tools/tools/spirv-opt" "$work/spirv-tools/tools/spirv-dis" "$out/bin/"
cp "$work/glslang/StandAlone/glslang" "$out/bin/glslangValidator"
printf '%s\n' "$stamp" >"$out/.revisions"
"$out/bin/spirv-opt" --help | grep -q -- --canonicalize-ids ||
    { echo "build-shader-tools: the spirv-opt built has no --canonicalize-ids" >&2; exit 1; }
echo "==> [shader-tools] $out/bin: $("$out/bin/spirv-opt" --version 2>&1 | head -n 1); $("$out/bin/glslangValidator" --version 2>&1 | head -n 1)"
