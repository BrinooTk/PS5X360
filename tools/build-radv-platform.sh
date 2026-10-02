#!/usr/bin/env bash
# Build the reference RADV recipe from exact revisions in isolated caches.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
reference="$root/.deps/references/PS5_Vulkan"
export PS5_MESA_FORK="$root/.deps/references/PS5_Mesa"
export PS5_PAYLOAD_SDK_FORK="$root/.deps/references/PS5_PayloadSDK"
expected_reference=3f3ee69607013b345d2baa6d6a37c86745649a08
[[ $(git -C "$reference" rev-parse HEAD) == "$expected_reference" ]] || { echo 'Vulkan reference revision mismatch' >&2; exit 1; }
git -C "$PS5_MESA_FORK" cat-file -e 0b2d6d1a61d9bbf89cf8beb88a696144f67c61f8^{commit}
git -C "$PS5_PAYLOAD_SDK_FORK" cat-file -e 95c08f27386fc698f6bbe21dde3030140a41d10b^{commit}
mkdir -p "$reference/.deps/work"
wrapper="$reference/.deps/work/ninja-limited"
printf '#!/bin/sh\nexec ninja -j 4 "$@"\n' > "$wrapper"
chmod +x "$wrapper"
export NINJA="$wrapper" BUILD_JOBS=4
bash "$reference/tools/setup-native-dependencies.sh"
bash "$reference/tools/build-radv.sh" release
cp "$reference/.deps/native/radv-release/PROVENANCE.txt" "$root/build/radv-provenance.txt"
