// SPDX-License-Identifier: MIT
#pragma once
#include <cstdio>
#include <string>
#include <string_view>
namespace xe::kernel {
std::string ResolveUserModulePath(std::string_view requested,
                                 std::string_view executable_path);
}
namespace xbox360ps5 {
inline int CheckModulePaths() {
  struct Case { const char* requested; const char* base; const char* expected; };
  const Case cases[] = {
      {"WavesLibDLL.dll", "game:\\default.xex", "game:\\WavesLibDLL.dll"},
      {"sub\\engine.xex", "game:\\default.xex", "game:\\sub\\engine.xex"},
      {"sub/engine.xex", "game:\\default.xex", "game:\\sub\\engine.xex"},
      {"cache:\\engine.xex", "game:\\default.xex", "cache:\\engine.xex"},
      {"\\Device\\Harddisk0\\engine.xex", "game:\\default.xex", "\\Device\\Harddisk0\\engine.xex"},
      {"engine.xex", "", "engine.xex"},
      {"engine.xex", "game:\\sub\\default.xex", "game:\\sub\\engine.xex"},
      {"", "game:\\default.xex", ""},
  };
  for (const auto& item : cases) {
    const auto got = xe::kernel::ResolveUserModulePath(item.requested, item.base);
    if (got != item.expected) {
      std::fprintf(stderr, "FAIL: module path '%s': got '%s', expected '%s'\n",
                   item.requested, got.c_str(), item.expected);
      return 1;
    }
  }
  puts("PASS: relative module names/subdirectories, qualified and empty paths");
  return 0;
}
}
