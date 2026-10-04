// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include "xenia/base/logging.h"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/vfs/virtual_file_system.h"
namespace xbox360ps5 {
inline bool MountUtilityCache(xe::vfs::VirtualFileSystem& fs,
                              const std::filesystem::path& storage) {
  // Match the desktop frontend's order: CACHE must follow CACHE0/CACHE1,
  // since VFS device resolution matches mount prefixes.
  for (const char* name : {"cache0", "cache1", "cache"}) {
    const auto root = storage / "utility-cache" / name;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) { XELOGE("Utility cache {}: {}", root.string(), error.message()); return false; }
    std::string mount = "\\" + std::string(name);
    for (auto& c : mount) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    auto device = std::make_unique<xe::vfs::HostPathDevice>(mount, root, false);
    if (!device->Initialize() || !fs.RegisterDevice(std::move(device)) ||
        !fs.RegisterSymbolicLink(std::string(name) + ":", mount)) {
      XELOGE("Utility cache {} registration failed", name);
      return false;
    }
    XELOGI("Utility cache {}: -> {}", name, root.string());
  }
  return true;
}
}
