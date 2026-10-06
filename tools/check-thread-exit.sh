#!/usr/bin/env bash
# AddressSanitizer on the actual POSIX thread implementation, no guest ROM needed.
set -euo pipefail
out=build/thread-exit-check
mkdir -p "$out"
clang++-18 -std=c++20 -DCHECK_FIXED_EXIT -g -O1 -fsanitize=address \
  -ffunction-sections -fdata-sections -I.deps/xenia-canary -I.deps/xenia-canary/src \
  -I.deps/xenia-canary/third_party -I.deps/xenia-canary/third_party/fmt/include \
  tools/check-thread-exit.cpp .deps/xenia-canary/src/xenia/base/threading_posix.cc \
  -fuse-ld=lld -flto=thin -Wl,--gc-sections \
  build/canary-runner/obj/Linux/libxenia-base.a build/canary-runner/obj/Linux/libfmt.a \
  -lpthread -ldl -o "$out/check"
ASAN_OPTIONS=detect_leaks=0 "$out/check"
# Confirm the reproducer still detects the original release-before-exit order.
if ASAN_OPTIONS=detect_leaks=0 "$out/check" original > "$out/original.log" 2>&1; then
  echo "Original lifetime regression unexpectedly passed" >&2; exit 1
fi
grep -q 'heap-use-after-free' "$out/original.log"
echo 'PASS: original order fails with use-after-free; corrected order completes'
