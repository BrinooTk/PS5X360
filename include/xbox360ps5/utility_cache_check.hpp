// SPDX-License-Identifier: MIT
#pragma once
#include "xbox360ps5/utility_cache.hpp"
#include "xenia/base/filesystem.h"
#include "xenia/vfs/entry.h"
#include "xenia/vfs/file.h"
#include <array>
#include <cstdlib>
#include <stdexcept>
namespace xbox360ps5 {
inline int CheckUtilityCache() {
  char pattern[] = "/tmp/ps5x360-utility-cache-XXXXXX";
  const char* made = mkdtemp(pattern);
  if (!made) return 1;
  const std::filesystem::path root(made);
  const auto require = [](bool value) { if (!value) throw std::runtime_error("Utility cache regression"); };
  {
    xe::vfs::VirtualFileSystem fs;
    require(fs.ResolvePath("cache:\\valid.txt") == nullptr);
    require(MountUtilityCache(fs, root));
    uint8_t value = 1;
    for (const char* name : {"cache0", "cache1", "cache"}) {
      const std::string path = std::string(name) + ":\\valid.txt";
      auto* entry = fs.CreatePath(path, 0x80);
      require(entry != nullptr);
      xe::vfs::File* file = nullptr;
      require(entry->Open(xe::filesystem::FileAccess::kGenericRead | xe::filesystem::FileAccess::kGenericWrite, &file) == 0);
      std::unique_ptr<xe::vfs::File> owned(file);
      const std::array<uint8_t, 1> data{value++};
      size_t written = 0;
      require(file->WriteSync(data, 0, &written) == 0 && written == 1);
    }
  }
  {
    xe::vfs::VirtualFileSystem fs;
    require(MountUtilityCache(fs, root));
    uint8_t expected = 1;
    for (const char* name : {"cache0", "cache1", "cache"}) {
      auto* entry = fs.ResolvePath(std::string(name) + ":\\valid.txt");
      require(entry != nullptr);
      xe::vfs::File* file = nullptr;
      require(entry->Open(xe::filesystem::FileAccess::kGenericRead, &file) == 0);
      std::unique_ptr<xe::vfs::File> owned(file);
      std::array<uint8_t, 1> data{};
      size_t read = 0;
      require(file->ReadSync(data, 0, &read) == 0 && read == 1 && data[0] == expected++);
    }
  }
  std::filesystem::remove_all(root); // Only this mkdtemp fixture.
  puts("PASS: missing cache reproduced; three writable guest cache devices persist separately across remounts");
  return 0;
}
}
