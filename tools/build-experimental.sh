#!/usr/bin/env bash
# Separate diagnostics candidate; never stages or deploys its executable.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export XBOX360PS5_EXPERIMENTAL_DIAGNOSTICS=ON
export XBOX360PS5_VERSION_OVERRIDE=0.5.8-experimental.1
export XBOX360PS5_OUTPUT_DIR="$root/build/canary-game-experimental"
bash "$root/tools/build-canary-game.sh"
