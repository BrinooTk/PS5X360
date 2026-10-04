// SPDX-License-Identifier: MIT
#pragma once
#include "xenia/base/mapped_memory.h"
#include "xenia/emulator.h"
#include "xenia/vfs/devices/disc_image_device.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>
namespace xbox360ps5 {
inline int CheckFileOpening() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() /
      ("xbox360ps5-file-check-" + std::to_string(getpid()));
  fs::create_directories(root);
  const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  const auto path = root / "ordinary-data.bin";
  std::vector<char> contents(page * 2, 'a');
  std::fill(contents.begin() + page, contents.end(), 'b');
  { std::ofstream output(path, std::ios::binary);
    output.write(contents.data(), contents.size()); }
  // A zero length means the remainder of the file, not the entire file again.
  auto tail = xe::MappedMemory::Open(path, xe::MappedMemory::Mode::kRead, page);
  if (!tail || tail->size() != page || tail->data()[0] != 'b' ||
      tail->data()[page - 1] != 'b') {
    std::fprintf(stderr, "FAIL: mapped tail size %zu, expected %zu\n",
                 tail ? tail->size() : 0, page);
    return 1;
  }
  tail.reset();
  auto whole = xe::MappedMemory::Open(path, xe::MappedMemory::Mode::kRead);
  if (!whole || whole->size() != contents.size() || whole->data()[0] != 'a') return 2;
  whole.reset();
  const auto empty = root / "empty.bin";
  std::ofstream(empty, std::ios::binary).close();
  xe::Emulator emulator("", root, root / "content", root / "cache");
  auto descriptor_count = []() {
    size_t count = 0;
    for (const auto& unused : fs::directory_iterator("/proc/self/fd")) ++count;
    return count;
  };
  const auto before = descriptor_count();
  for (int i = 0; i < 2; ++i) {
    if (emulator.GetFileSignature(empty) != xe::Emulator::FileSignatureType::Unknown) return 3;
  }
  const auto after = descriptor_count();
  if (after != before) {
    std::fprintf(stderr, "FAIL: empty-file probing leaks %zu file descriptors\n", after - before);
    return 4;
  }
  // Small ordinary disc fixtures cover every partition offset already
  // supported by the full mount. Each has one normal default.xex entry.
  const size_t partitions[] = {0, 0xFB20, 0x20600, 0x2080000, 0xFD90000};
  for (size_t partition : partitions) {
    const auto disc_path = root / (std::to_string(partition) + ".iso");
    std::vector<unsigned char> disc(35 * 2048, 0);
    auto store32 = [&disc](size_t at, uint32_t value) {
      for (unsigned n = 0; n < 4; ++n) disc[at + n] = uint8_t(value >> (8 * n));
    };
    std::memcpy(disc.data() + 32 * 2048, "MICROSOFT*XBOX*MEDIA", 20);
    store32(32 * 2048 + 20, 33);
    store32(32 * 2048 + 24, 32);
    const size_t entry = 33 * 2048;
    store32(entry + 4, 34);
    store32(entry + 8, 4);
    disc[entry + 12] = 0x20;
    disc[entry + 13] = 11;
    std::memcpy(disc.data() + entry + 14, "default.xex", 11);
    std::memcpy(disc.data() + 34 * 2048, "XEX2", 4);
    { std::ofstream output(disc_path, std::ios::binary);
      output.seekp(partition);
      output.write(reinterpret_cast<const char*>(disc.data()), disc.size()); }
    if (emulator.GetFileSignature(disc_path) != xe::Emulator::FileSignatureType::XISO) return 5;
    if (partition == 0) {
      xe::vfs::DiscImageDevice mounted("\\Device\\FixtureDisc", disc_path);
      if (!mounted.Initialize() || !mounted.ResolvePath("default.xex")) return 6;
    }
  }
  const auto xex = root / "signature.xex";
  { std::ofstream output(xex, std::ios::binary); output.write("XEX2", 4); }
  if (emulator.GetFileSignature(xex) != xe::Emulator::FileSignatureType::XEX2) return 7;
  const auto package = root / "signature.package";
  { std::ofstream output(package, std::ios::binary); output.write("LIVE", 4); }
  if (emulator.GetFileSignature(package) != xe::Emulator::FileSignatureType::LIVE) return 8;
  if (emulator.GetFileSignature(root / "missing.bin") != xe::Emulator::FileSignatureType::Unknown) return 9;
  fs::remove_all(root);
  puts("PASS: mapping lengths, probe descriptor ownership, five ISO offsets, actual disc mount, XEX/LIVE detection and missing files");
  return 0;
}
}
