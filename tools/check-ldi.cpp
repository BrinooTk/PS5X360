// SPDX-License-Identifier: MIT
// Real uncompressed LZX block, decoded through the production LDI adapter.
#include "xenia/kernel/util/ldi_decompressor.h"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
// Standalone diagnostic sink; the production decoder and libmspack are linked.
extern "C" void xenia_log(const char*, ...) {}
int main() {
  using xe::kernel::util::LdiDecompressor;
  assert(!LdiDecompressor::Create(0));
  assert(!LdiDecompressor::Create(12345));
  auto decoder = LdiDecompressor::Create(32768);
  assert(decoder);
  std::array<uint8_t, 28> block = {
    0x00,0x30,0xc0,0x00, // no E8 transform, raw block of 12 bytes
    1,0,0,0, 1,0,0,0, 1,0,0,0,
    'h','e','l','l','o',' ','w','o','r','l','d','!'
  };
  std::array<uint8_t, 16> output;
  output.fill(0xa5);
  uint32_t size = 12;
  assert(decoder->Decompress(block.data(), block.size(), output.data()+2, size));
  assert(size == 12 && !std::memcmp(output.data()+2, "hello world!", 12));
  assert(output[0]==0xa5 && output[1]==0xa5 && output[14]==0xa5 && output[15]==0xa5);
  assert(decoder->Reset());
  size=12;
  assert(decoder->Decompress(block.data(), block.size(), output.data()+2, size));
  size=32769;
  assert(!decoder->Decompress(block.data(), block.size(), output.data(), size));
  size=0;
  assert(!decoder->Decompress(block.data(), block.size(), output.data(), size));
  std::puts("PASS: production LDI decodes known LZX data, preserves guards, resets and rejects invalid output lengths");
}
