// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "xenia/base/mapped_memory.h"

namespace xbox360ps5 {

struct SmbUrl {
  std::string domain;
  std::string username;
  std::string password;
  std::string host;
  uint16_t port = 445;
  std::string share;
  std::string path;  // Leading slash stripped, forward slashes, e.g. "Games/Halo3/default.xex"
};

struct SmbDirEntry {
  std::string name;
  uint64_t size = 0;
  bool is_directory = false;
};

bool IsSmbPath(std::string_view path);

template <typename Path,
          typename = std::enable_if_t<std::is_same_v<std::decay_t<Path>, std::filesystem::path>>>
inline bool IsSmbPath(const Path& path) {
  return IsSmbPath(std::string_view(path.generic_string()));
}
// Parses "smb://[domain;user[:pass]@]host[:port]/share[/path]"
bool ParseSmbUrl(std::string_view url, SmbUrl& out);

// Joins an smb:// base URL with a relative child name or subpath.
std::string JoinSmbUrl(std::string_view base_url, std::string_view child);

// Lists entries in an smb:// directory. Returns false on network/auth failure.
bool SmbListDirectory(std::string_view url, std::vector<SmbDirEntry>& out_entries);

// Queries file size and directory flag for an smb:// path.
bool SmbStat(std::string_view url, uint64_t& out_size, bool& out_is_directory);

// Reads up to `length` bytes at `offset` from an smb:// file using the read-ahead cache.
bool SmbReadAt(std::string_view url, uint64_t offset, void* buffer, size_t length, size_t* out_read = nullptr);

// Creates a non-mapped MappedMemory backed by SMB2 pread + 256 KiB LRU chunk cache.
// Works seamlessly with Xenia's DiscImageDevice (ISO), StfsContainerDevice, and SvodContainerDevice (GOD).
std::unique_ptr<xe::MappedMemory> OpenSmbMappedMemory(std::string_view url);

// Closes idle SMB2 sessions (call when returning to launcher or changing paths).
void SmbCloseIdleSessions();

}  // namespace xbox360ps5
