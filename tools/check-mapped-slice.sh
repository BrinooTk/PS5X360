#!/usr/bin/env bash
# The file views packages and disc images are read through on the console: a file opened without being
# mapped (MappedMemory::OpenForReading), parts of it and parts of parts, under AddressSanitizer on the
# production mapped_memory_posix.cc. Run in the host build image; needs build/canary-runner.
set -euo pipefail
out=build/mapped-slice-check
mkdir -p "$out"
cat > "$out/check.cpp" <<'EOF'
#include "xenia/base/mapped_memory.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
// The byte a file of this check holds at an offset.
static uint8_t At(size_t offset) { return uint8_t(offset * 131 + (offset >> 8) * 7 + (offset >> 16)); }
static bool Holds(const xe::MappedMemory& view, size_t offset, size_t length, size_t file_offset) {
  std::vector<uint8_t> bytes(length);
  if (!view.ReadAt(offset, bytes.data(), length)) return false;
  for (size_t n = 0; n < length; ++n) if (bytes[n] != At(file_offset + n)) return false;
  return true;
}
int main(int, char** argv) {
  const size_t size = 3u << 20;
  {
    std::ofstream file(argv[1], std::ios::binary | std::ios::trunc);
    std::vector<char> bytes(size);
    for (size_t n = 0; n < size; ++n) bytes[n] = char(At(n));
    file.write(bytes.data(), std::streamsize(size));
  }
  setenv("XE_MAPPED_READ_FILE", "1", 1);  // The console's way, on this machine.
  auto file = xe::MappedMemory::OpenForReading(argv[1]);
  assert(file && !file->is_mapped() && file->size() == size);
  assert(Holds(*file, 0, 4096, 0) && Holds(*file, size - 100, 100, size - 100));
  uint8_t byte;
  assert(!file->ReadAt(size, &byte, 1) && !file->ReadAt(size - 1, &byte, 2));
  // A package's data: everything after its header. Then a part of that.
  auto data = file->Slice(0xB000, size - 0xB000);
  assert(data && !data->is_mapped() && data->size() == size - 0xB000);
  assert(Holds(*data, 0, 0x1000, 0xB000) && Holds(*data, 0x1000, 0x2000, 0xC000));
  assert(!data->ReadAt(data->size(), &byte, 1));
  auto inner = data->Slice(0x3000, 0x800);
  assert(inner && Holds(*inner, 0x10, 0x100, 0xE010) && !inner->ReadAt(0x7FF, &byte, 2));
  // What does not fit is refused, not mounted with nothing behind it.
  assert(!file->Slice(size + 1, 0) && !file->Slice(1, size) && !data->Slice(data->size(), 1));
  assert(file->Slice(size, 0));
  // More parts than a process may hold open files (the limit here is 1024):
  // each used to take a descriptor of its own and came back null without one.
  std::vector<std::unique_ptr<xe::MappedMemory>> many;
  for (size_t n = 0; n < 3000; ++n) {
    many.push_back(file->Slice(n * 512, 512));
    assert(many.back() && Holds(*many.back(), 0, 512, n * 512));
  }
  // A part outlives the view it came from, closed or destroyed.
  file->Close();
  assert(Holds(*data, 0x1000, 0x2000, 0xC000) && Holds(*inner, 0, 0x800, 0xE000));
  assert(!file->Slice(0, 16));  // A closed view has no parts, and does not crash.
  file.reset();
  many.clear();
  assert(Holds(*data, 5, 4096, 0xB005) && Holds(*inner, 0x7FF, 1, 0xE7FF));
  data.reset();
  assert(Holds(*inner, 0, 0x800, 0xE000));
  std::puts("PASS: an unmapped file, its parts and parts of parts read the right bytes; 3000 parts of one file; "
            "parts outlive a closed and a destroyed parent; out-of-range parts and reads are refused");
}
EOF
clang++-18 -std=c++20 -g -O1 -fsanitize=address,undefined \
  -ffunction-sections -fdata-sections -I.deps/xenia-canary -I.deps/xenia-canary/src \
  -I.deps/xenia-canary/third_party -I.deps/xenia-canary/third_party/fmt/include \
  "$out/check.cpp" .deps/xenia-canary/src/xenia/base/mapped_memory_posix.cc \
  -fuse-ld=lld -flto=thin -Wl,--gc-sections \
  build/canary-runner/obj/Linux/libxenia-base.a build/canary-runner/obj/Linux/libfmt.a \
  -lpthread -ldl -o "$out/check"
ASAN_OPTIONS=detect_leaks=0 "$out/check" "$out/sample.bin"
