// SPDX-License-Identifier: MIT
// Xenia memory contract implemented with ordinary mmap/mprotect APIs.
// A denied allocation is reported to the caller; this adapter changes no
// process privileges and never replaces mappings it does not own.
#include "xenia/base/memory.h"
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <map>
#include <mutex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace xe::memory {
namespace {
struct Region { size_t length; int handle; std::vector<PageAccess> pages; };
std::map<uintptr_t, Region> regions;
std::map<int, std::filesystem::path> mapping_files;
std::mutex guard;
int protection(PageAccess a) {
  switch (a) {
    case PageAccess::kNoAccess: return PROT_NONE;
    case PageAccess::kReadOnly: return PROT_READ;
    case PageAccess::kReadWrite: return PROT_READ | PROT_WRITE;
    case PageAccess::kExecuteReadOnly: return PROT_READ | PROT_EXEC;
    case PageAccess::kExecuteReadWrite: return PROT_READ | PROT_WRITE | PROT_EXEC;
  }
  return -1;
}
size_t rounded(size_t n) {
  const size_t p = page_size();
  return n && n <= SIZE_MAX - (p - 1) ? (n + p - 1) / p * p : 0;
}
auto containing(uintptr_t address, size_t length) {
  auto i = regions.upper_bound(address);
  if (i == regions.begin()) return regions.end();
  --i;
  const auto offset = address - i->first;
  return offset < i->second.length && length <= i->second.length - offset ? i : regions.end();
}
void* map_exact(void* address, size_t length, PageAccess access, int handle, size_t offset) {
  const int prot = protection(access);
  const size_t size = rounded(length);
  if (!size || prot < 0 || uintptr_t(address) % page_size() || offset % page_size()) return nullptr;
  // A hint without MAP_FIXED cannot clobber an existing mapping. Reject a
  // relocated result when the caller requires an exact guest/code address.
  void* result = mmap(address, size, prot,
      handle < 0 ? MAP_PRIVATE | MAP_ANONYMOUS : MAP_SHARED, handle, offset);
  if (result == MAP_FAILED) return nullptr;
  if (address && result != address) { munmap(result, size); return nullptr; }
  try {
    regions.emplace(uintptr_t(result), Region{size, handle,
        std::vector<PageAccess>(size / page_size(), access)});
  } catch (...) { munmap(result, size); throw; }
  return result;
}
bool protect_owned(uintptr_t address, size_t length, PageAccess access, PageAccess* previous) {
  const size_t p = page_size(), leading = address % p;
  if (!length || length > SIZE_MAX - leading) return false;
  address -= leading;
  length = rounded(length + leading);
  if (!length || protection(access) < 0) return false;
  auto i = containing(address, length);
  if (i == regions.end()) return false;
  const size_t first = (address - i->first) / p, count = length / p;
  const auto old = i->second.pages[first];
  if (previous) {
    for (size_t n = 1; n < count; ++n) if (i->second.pages[first + n] != old) return false;
  }
  if (mprotect(reinterpret_cast<void*>(address), length, protection(access))) return false;
  if (previous) *previous = old;
  std::fill_n(i->second.pages.begin() + first, count, access);
  return true;
}
}

size_t page_size() { static const size_t p = static_cast<size_t>(getpagesize()); return p; }
size_t allocation_granularity() { return page_size(); }
bool IsWritableExecutableMemorySupported() {
  // Capability check through the normal API, never a privilege workaround.
  static const bool supported = [] {
    const size_t p = page_size();
    void* memory = mmap(nullptr, p, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) return false;
    munmap(memory, p); return true;
  }();
  return supported;
}
void* AllocFixed(void* address, size_t length, AllocationType type, PageAccess access) {
  std::lock_guard lock(guard);
  if (type == AllocationType::kCommit)
    return address && protect_owned(uintptr_t(address), length, access, nullptr) ? address : nullptr;
  if (type != AllocationType::kReserve && type != AllocationType::kReserveCommit) return nullptr;
  return map_exact(address, length, type == AllocationType::kReserve ? PageAccess::kNoAccess : access, -1, 0);
}
bool DeallocFixed(void* address, size_t length, DeallocationType type) {
  std::lock_guard lock(guard);
  if (type == DeallocationType::kDecommit) {
    auto i = containing(uintptr_t(address), length);
    if (i == regions.end() || i->second.handle >= 0 || uintptr_t(address) % page_size() || length % page_size()) return false;
    if (!protect_owned(uintptr_t(address), length, PageAccess::kNoAccess, nullptr)) return false;
    return madvise(address, length, MADV_DONTNEED) == 0;
  }
  auto i = regions.find(uintptr_t(address));
  if (type != DeallocationType::kRelease || length != 0 || i == regions.end() || i->second.handle >= 0) return false;
  if (munmap(address, i->second.length)) return false;
  regions.erase(i); return true;
}
bool Protect(void* address, size_t length, PageAccess access, PageAccess* previous) {
  std::lock_guard lock(guard); return protect_owned(uintptr_t(address), length, access, previous);
}
bool QueryProtect(void* address, size_t& length, PageAccess& access) {
  std::lock_guard lock(guard);
  auto i = containing(uintptr_t(address), 1);
  if (i == regions.end()) return false;
  size_t first = (uintptr_t(address) - i->first) / page_size(), end = first + 1;
  access = i->second.pages[first];
  while (end < i->second.pages.size() && i->second.pages[end] == access) ++end;
  length = (end - first) * page_size(); return true;
}
FileMappingHandle CreateFileMappingHandle(const std::filesystem::path& path, size_t length, PageAccess access, bool) {
  const size_t size = rounded(length);
  if (!size || protection(access) < 0 || size > size_t(std::numeric_limits<off_t>::max())) return kFileMappingHandleInvalid;
  std::filesystem::path filename = path;
#if XE_PLATFORM_PS5
  if (!filename.is_absolute()) filename = std::filesystem::path("/download0") / filename;
#endif
  int fd = open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0) return kFileMappingHandleInvalid;
  if (ftruncate(fd, size)) { close(fd); unlink(filename.c_str()); return kFileMappingHandleInvalid; }
  std::lock_guard lock(guard);
  mapping_files.emplace(fd, filename); return fd;
}
void CloseFileMappingHandle(FileMappingHandle handle, const std::filesystem::path&) {
  std::lock_guard lock(guard);
  auto i = mapping_files.find(handle);
  if (i == mapping_files.end()) return;
  close(handle); unlink(i->second.c_str()); mapping_files.erase(i);
}
void* MapFileView(FileMappingHandle handle, void* address, size_t length, PageAccess access, size_t offset) {
  std::lock_guard lock(guard);
  if (mapping_files.find(handle) == mapping_files.end()) return nullptr;
  struct stat status{};
  const size_t size = rounded(length);
  if (!size || fstat(handle, &status) || status.st_size < 0 ||
      offset > size_t(status.st_size) || size > size_t(status.st_size) - offset) return nullptr;
  return map_exact(address, length, access, handle, offset);
}
bool UnmapFileView(FileMappingHandle handle, void* address, size_t length) {
  std::lock_guard lock(guard);
  auto i = regions.find(uintptr_t(address));
  if (i == regions.end() || i->second.handle != handle || i->second.length != rounded(length)) return false;
  if (munmap(address, i->second.length)) return false;
  regions.erase(i); return true;
}
}
