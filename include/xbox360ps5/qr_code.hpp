// SPDX-License-Identifier: MIT
// A QR code for a short text (the address of the settings page): byte mode,
// error correction level L, versions 1 to 5 (one block each, up to 106 bytes),
// mask pattern 0. Written from the public specification (ISO/IEC 18004);
// tools/check-qr-code.sh decodes its output with an independent reader.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace xbox360ps5 {
struct QrCode {
  int size = 0;  // Modules a side; 0 when the text does not fit.
  std::vector<uint8_t> dark;
  bool At(int x, int y) const { return dark[size_t(y) * size_t(size) + size_t(x)] != 0; }
};
inline QrCode MakeQrCode(const std::string& text) {
  static constexpr int kData[] = {0, 19, 34, 55, 80, 108}, kCheck[] = {0, 7, 10, 15, 20, 26};
  int version = 0;
  for (int v = 1; v <= 5 && !version; ++v) if (int(text.size()) + 2 <= kData[v]) version = v;
  QrCode code;
  if (!version) return code;
  // The data: mode 0100, an 8-bit count, the bytes, a terminator, then padding.
  std::vector<uint8_t> words;
  uint32_t held = 0; int bits = 0;
  const auto put = [&](uint32_t value, int count) {
    held = held << count | value; bits += count;
    while (bits >= 8) { words.push_back(uint8_t(held >> (bits - 8))); bits -= 8; }
  };
  put(4, 4); put(uint32_t(text.size()), 8);
  for (const unsigned char c : text) put(c, 8);
  put(0, 4);
  if (bits) put(0, 8 - bits);
  for (int pad = 0; int(words.size()) < kData[version]; ++pad) words.push_back(pad & 1 ? 0x11 : 0xEC);
  // Reed-Solomon check words over GF(256), polynomial 0x11D.
  const auto times = [](uint8_t a, uint8_t b) {
    int product = 0;
    for (int i = 7; i >= 0; --i) { product = (product << 1) ^ ((product >> 7) * 0x11D); if (b >> i & 1) product ^= a; }
    return uint8_t(product);
  };
  const int check = kCheck[version];
  std::vector<uint8_t> divisor(size_t(check), 0);
  divisor.back() = 1;
  uint8_t root = 1;
  for (int i = 0; i < check; ++i) {
    for (int j = 0; j < check; ++j) {
      divisor[size_t(j)] = times(divisor[size_t(j)], root);
      if (j + 1 < check) divisor[size_t(j)] ^= divisor[size_t(j) + 1];
    }
    root = times(root, 2);
  }
  std::vector<uint8_t> remainder(size_t(check), 0);
  for (const uint8_t word : words) {
    const uint8_t factor = word ^ remainder.front();
    remainder.erase(remainder.begin());
    remainder.push_back(0);
    for (int j = 0; j < check; ++j) remainder[size_t(j)] ^= times(divisor[size_t(j)], factor);
  }
  words.insert(words.end(), remainder.begin(), remainder.end());
  // The fixed patterns.
  const int size = 17 + 4 * version;
  code.size = size;
  code.dark.assign(size_t(size) * size_t(size), 0);
  std::vector<uint8_t> fixed(code.dark.size(), 0);
  const auto set = [&](int x, int y, bool on) {
    if (x < 0 || y < 0 || x >= size || y >= size) return;
    code.dark[size_t(y) * size_t(size) + size_t(x)] = on;
    fixed[size_t(y) * size_t(size) + size_t(x)] = 1;
  };
  for (int i = 0; i < size; ++i) { set(6, i, i % 2 == 0); set(i, 6, i % 2 == 0); }
  const auto finder = [&](int cx, int cy) {
    for (int dy = -4; dy <= 4; ++dy) for (int dx = -4; dx <= 4; ++dx) {
      const int ring = dx < 0 ? (-dx > (dy < 0 ? -dy : dy) ? -dx : (dy < 0 ? -dy : dy)) : (dx > (dy < 0 ? -dy : dy) ? dx : (dy < 0 ? -dy : dy));
      set(cx + dx, cy + dy, ring != 2 && ring != 4);
    }
  };
  finder(3, 3); finder(size - 4, 3); finder(3, size - 4);
  if (version >= 2) {
    const int centre = 4 * version + 10;
    for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
      const int ring = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
      set(centre + dx, centre + dy, ring != 1);
    }
  }
  // Format information: level L (01), mask 0, with its BCH check, twice.
  const int format_data = 1 << 3 | 0;
  int rest = format_data;
  for (int i = 0; i < 10; ++i) rest = (rest << 1) ^ ((rest >> 9) * 0x537);
  const int format = (format_data << 10 | rest) ^ 0x5412;
  const auto bit = [format](int i) { return (format >> i & 1) != 0; };
  for (int i = 0; i <= 5; ++i) set(8, i, bit(i));
  set(8, 7, bit(6)); set(8, 8, bit(7)); set(7, 8, bit(8));
  for (int i = 9; i < 15; ++i) set(14 - i, 8, bit(i));
  for (int i = 0; i < 8; ++i) set(size - 1 - i, 8, bit(i));
  for (int i = 8; i < 15; ++i) set(8, size - 15 + i, bit(i));
  set(8, size - 8, true);
  // The words, in the zigzag from the bottom right, under mask 0.
  size_t at = 0;
  for (int right = size - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vertical = 0; vertical < size; ++vertical) {
      for (int j = 0; j < 2; ++j) {
        const int x = right - j;
        const bool upward = ((right + 1) & 2) == 0;
        const int y = upward ? size - 1 - vertical : vertical;
        const size_t index = size_t(y) * size_t(size) + size_t(x);
        if (fixed[index]) continue;
        bool on = false;
        if (at < words.size() * 8) { on = (words[at >> 3] >> (7 - (at & 7)) & 1) != 0; ++at; }
        code.dark[index] = on != ((x + y) % 2 == 0);
      }
    }
  }
  return code;
}
}
