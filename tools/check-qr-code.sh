#!/usr/bin/env bash
# Renders QR codes with include/xbox360ps5/qr_code.hpp and reads them back with zbar, an independent
# decoder. Run in a disposable container: it installs zbar-tools when zbarimg is missing.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/qr-code-check"
mkdir -p "$out"
cat > "$out/render.cpp" <<'EOF'
#include "xbox360ps5/qr_code.hpp"
#include <cstdio>
int main(int argc, char** argv) {
  const auto code = xbox360ps5::MakeQrCode(argv[1]);
  if (!code.size) return 3;
  // A greyscale picture: eight pixels a module, four modules of quiet zone.
  const int scale = 8, side = (code.size + 8) * scale;
  std::FILE* file = std::fopen(argv[2], "wb");
  std::fprintf(file, "P5\n%d %d\n255\n", side, side);
  for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
    const int mx = x / scale - 4, my = y / scale - 4;
    const bool dark = mx >= 0 && my >= 0 && mx < code.size && my < code.size && code.At(mx, my);
    std::fputc(dark ? 0 : 255, file);
  }
  std::fclose(file);
  std::printf("%d", code.size);
}
EOF
clang++-18 -std=c++20 -O1 -fsanitize=address,undefined -I"$root/include" "$out/render.cpp" -o "$out/render"
command -v zbarimg >/dev/null || { apt-get update -qq >/dev/null && apt-get install -y -qq zbar-tools >/dev/null; }
checked=0
for text in "A" "http://192.168.0.19:8360/?k=1a2b3c" "http://10.0.0.7:8360/" \
            "http://192.168.100.200:8367/?k=ffffff" "PS5X360 settings page: a longer text that needs version five of the code, 100 bytes!!"; do
  size=$("$out/render" "$text" "$out/code.pgm")
  read=$(zbarimg --quiet --raw "$out/code.pgm")
  if [[ "$read" != "$text" ]]; then echo "FAIL: '$text' read back as '$read'"; exit 1; fi
  echo "read back ${size}x${size}: $text"
  checked=$((checked + 1))
done
if "$out/render" "$(printf 'x%.0s' {1..107})" "$out/code.pgm" >/dev/null; then echo "FAIL: 107 bytes must not fit"; exit 1; fi
echo "PASS: $checked codes of versions 1 to 5 read back by zbar; a text that is too long is refused"
