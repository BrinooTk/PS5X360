// SPDX-License-Identifier: MIT
#include "xenia/base/memory.h"
#include "xenia/base/mapped_memory.h"
#include <cstdio>
#include <filesystem>
#include <unistd.h>
using namespace xe::memory;
int main() {
  unsigned cases = 0, failures = 0;
  auto check = [&](const char* label, bool ok) { ++cases; failures += !ok; std::printf("%s %s\n", ok ? "PASS" : "FAIL", label); };
  const size_t p = page_size();
  auto* a = static_cast<unsigned char*>(AllocFixed(nullptr, p * 3, AllocationType::kReserve, PageAccess::kReadWrite));
  check("reserve returns owned region", a != nullptr);
  if (!a) return 1;
  PageAccess access{}; size_t length = 0;
  check("reserve is uncommitted", QueryProtect(a, length, access) && access == PageAccess::kNoAccess && length == p * 3);
  check("commit owned pages", AllocFixed(a, p * 2, AllocationType::kCommit, PageAccess::kReadWrite) == a);
  a[0] = 0xa5; a[p] = 0x7b;
  check("commit preserves prior bytes", AllocFixed(a, p, AllocationType::kCommit, PageAccess::kReadWrite) == a && a[0] == 0xa5);
  check("collision never overwrites existing region", !AllocFixed(a, p, AllocationType::kReserveCommit, PageAccess::kReadWrite) && a[0] == 0xa5);
  check("query stops at access boundary", QueryProtect(a, length, access) && access == PageAccess::kReadWrite && length == p * 2);
  check("protect reports previous access", Protect(a, 1, PageAccess::kReadOnly, &access) && access == PageAccess::kReadWrite);
  check("unaligned query covers containing page", QueryProtect(a + 3, length, access) && access == PageAccess::kReadOnly && length == p);
  check("mixed previous protections rejected", !Protect(a, p * 2, PageAccess::kReadWrite, &access));
  check("restore writable access", Protect(a, p, PageAccess::kReadWrite));
  check("commit outside reservation rejected", !AllocFixed(a + p * 3, p, AllocationType::kCommit, PageAccess::kReadWrite));
  check("unaligned reservation rejected", !AllocFixed(a + 1, p, AllocationType::kReserve, PageAccess::kNoAccess));
  check("decommit anonymous page", DeallocFixed(a, p, DeallocationType::kDecommit));
  check("recommit decommitted page", AllocFixed(a, p, AllocationType::kCommit, PageAccess::kReadWrite) == a);
  check("decommitted page is zero", a[0] == 0 && a[p] == 0x7b);
  check("partial release rejected", !DeallocFixed(a, p, DeallocationType::kRelease));
  check("zero-length release frees whole region", DeallocFixed(a, 0, DeallocationType::kRelease));
  check("released region no longer tracked", !QueryProtect(a, length, access));
  check("zero reservation rejected", !AllocFixed(nullptr, 0, AllocationType::kReserve, PageAccess::kNoAccess));
  check("overflow reservation rejected", !AllocFixed(nullptr, SIZE_MAX, AllocationType::kReserve, PageAccess::kNoAccess));
  auto path = std::filesystem::temp_directory_path() / ("xenia-memory-contract-" + std::to_string(getpid()));
  int fd = CreateFileMappingHandle(path, p * 2, PageAccess::kReadWrite, false);
  check("create shared backing file", fd >= 0);
  if (fd < 0) return 1;
  check("existing backing file never truncated", CreateFileMappingHandle(path, p, PageAccess::kReadWrite, false) < 0);
  auto* first = static_cast<unsigned char*>(MapFileView(fd, nullptr, p * 2, PageAccess::kReadWrite, 0));
  auto* second = static_cast<unsigned char*>(MapFileView(fd, nullptr, p, PageAccess::kReadWrite, p));
  check("file views map successfully", first && second);
  if (first && second) {
    first[p + 5] = 0x6c;
    check("file offset alias sees writes", second[5] == 0x6c);
    second[7] = 0x45;
    check("alias writes reach primary view", first[p + 7] == 0x45);
    check("commit preserves alias backing", AllocFixed(second, p, AllocationType::kCommit, PageAccess::kReadWrite) == second && second[5] == 0x6c);
    check("file decommit explicitly rejected", !DeallocFixed(second, p, DeallocationType::kDecommit));
    check("wrong view length rejected", !UnmapFileView(fd, first, p));
  }
  check("mapping beyond file rejected", !MapFileView(fd, nullptr, p, PageAccess::kReadWrite, p * 2));
  check("unaligned file offset rejected", !MapFileView(fd, nullptr, p, PageAccess::kReadWrite, 1));
  auto mapped = xe::MappedMemory::Open(path, xe::MappedMemory::Mode::kRead, p);
  check("mapped file reads remaining bytes from offset", mapped && mapped->size() == p && mapped->data()[5] == 0x6c);
  auto short_map = xe::MappedMemory::Open(path, xe::MappedMemory::Mode::kRead, p, 16);
  check("explicit file range retains size and shared contents", short_map && short_map->size() == 16 && short_map->data()[7] == 0x45);
  check("file mapping cannot extend past EOF", !xe::MappedMemory::Open(path, xe::MappedMemory::Mode::kRead, p, p + 1));
  check("empty mapping at EOF rejected", !xe::MappedMemory::Open(path, xe::MappedMemory::Mode::kRead, p * 2));
  check("overflow file offset rejected", !xe::MappedMemory::Open(path, xe::MappedMemory::Mode::kRead, SIZE_MAX));
  check("unaligned mapped file offset rejected", !xe::MappedMemory::Open(path, xe::MappedMemory::Mode::kRead, 1));
  mapped.reset(); short_map.reset();
  if (first) check("unmap first alias", UnmapFileView(fd, first, p * 2));
  if (second) check("unmap second alias", UnmapFileView(fd, second, p));
  CloseFileMappingHandle(fd, path);
  check("close removes owned backing file", !std::filesystem::exists(path));
  std::printf("MEMORY CASES %u FAILURES %u\n", cases, failures);
  return failures ? 1 : 0;
}
