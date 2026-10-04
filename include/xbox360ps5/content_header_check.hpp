// SPDX-License-Identifier: MIT
#pragma once
#include "xenia/kernel/xam/content_manager.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdio>
namespace xbox360ps5 {
inline int CheckContentHeaders() {
  using xe::X_STATUS;
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "xbox360ps5-header-regression";
  fs::create_directories(root);
  xe::kernel::xam::XContentContainerHeader header{};
  auto path = root / "profile" / "content.header";
  for (int n = 0; n < 2; ++n) {
    reinterpret_cast<unsigned char*>(&header)[sizeof(header)-1] = unsigned(n);
    if (xe::kernel::xam::ContentManager::ExtractContentHeader(path, header) != 0) return 1;
    std::ifstream input(path, std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(input)), {});
    if (data.size() != xe::round_up(sizeof(header), 0x1000) ||
        std::memcmp(data.data(), &header, sizeof(header))) return 2;
  }
  std::ofstream(root / "blocked-parent").put('x');
  try {
    if (xe::kernel::xam::ContentManager::ExtractContentHeader(root / "blocked-parent" / "nested" / "profile.header", header) == 0) return 3;
  } catch (...) {
    std::fprintf(stderr, "FAIL: content header write failure escapes as an exception\n");
    return 4;
  }
  const auto collision = root / "existing-directory";
  fs::create_directories(collision);
  std::ofstream(collision / "preserved.txt").put('s');
  try {
    if (xe::kernel::xam::ContentManager::ExtractContentHeader(collision, header) == 0) return 5;
  } catch (...) { return 6; }
  if (!fs::exists(path) || !fs::exists(collision / "preserved.txt") || fs::exists(root / "existing-directory.tmp")) return 7;
  // Linux /dev/full fails buffered writes at close with ENOSPC. Keep a prior
  // valid header to verify that the failure never replaces it.
  const auto full_path = root / "full.header";
  fs::copy_file(path, full_path, fs::copy_options::overwrite_existing);
  std::error_code fixture_error;
  fs::remove(root / "full.header.tmp", fixture_error);
  fs::create_symlink("/dev/full", root / "full.header.tmp");
  if (xe::kernel::xam::ContentManager::ExtractContentHeader(full_path, header) != X_STATUS_DISK_FULL) return 8;
  std::ifstream preserved(full_path, std::ios::binary);
  std::vector<char> preserved_data((std::istreambuf_iterator<char>(preserved)), {});
  if (preserved_data.size() != xe::round_up(sizeof(header), 0x1000) ||
      std::memcmp(preserved_data.data(), &header, sizeof(header))) return 9;
  std::puts("PASS: create/update padded header; failed write returns status without throwing; existing metadata retained");
  return 0;
}
}
