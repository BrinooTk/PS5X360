// SPDX-License-Identifier: MIT
#pragma once

#include <cstdio>
#include <cstring>
#include "xenia/gpu/shared_memory.h"

namespace xbox360ps5 {
// Exercise the actual upload-range builder against isolated guest 4 KiB
// allocations. A 16 KiB upload must not pull inaccessible neighbouring pages.
class UploadCheck final : public xe::gpu::SharedMemory {
 public:
  explicit UploadCheck(xe::Memory& memory) : SharedMemory(memory) {}
  bool Initialize() { return InitializeCommon(); }
  uint32_t uploaded_bytes = 0;
 protected:
  bool UploadRanges(const std::pair<uint32_t, uint32_t>* ranges,
                    uint32_t count) override {
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t start = ranges[i].first << page_size_log2();
      const uint32_t size = ranges[i].second << page_size_log2();
      if (memory().GetPhysicalHeap()->QueryRangeAccess(start, start + size - 1) ==
          xe::memory::PageAccess::kNoAccess) return false;
      MakeRangeValid(start, size, false);
      uploaded_bytes += size;
    }
    return true;
  }
};

inline int CheckGpuUploadPages() {
  xe::Memory memory;
  if (!memory.Initialize()) return 2;
  auto* heap = memory.GetPhysicalHeap();
  int failed = 0;
  // A freed thread stack can leave a guard inside the next, larger stack.
  // Recommitting the range must restore host access, including that guard.
  auto* stack_heap = memory.LookupHeap(0x41000000);
  constexpr uint32_t stack_address = 0x41000000;
  if (!stack_heap->AllocFixed(stack_address, 0x20000, 0x10000,
      xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
      xe::kMemoryProtectRead | xe::kMemoryProtectWrite)) return 2;
  if (!stack_heap->Protect(stack_address + 0x10000, 0x10000,
                          xe::kMemoryProtectNoAccess)) return 2;
  if (!stack_heap->Release(stack_address)) return 2;
  if (!stack_heap->AllocFixed(stack_address, 0x30000, 0x10000,
      xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
      xe::kMemoryProtectRead | xe::kMemoryProtectWrite)) return 2;
  size_t query_size = 1;
  xe::memory::PageAccess access = xe::memory::PageAccess::kNoAccess;
  const bool restored = xe::memory::QueryProtect(
      memory.TranslateVirtual(stack_address + 0x1FFF0), query_size, access) &&
      access == xe::memory::PageAccess::kReadWrite;
  std::printf("Guest stack recommit: %s\n", restored ? "PASS" : "FAIL");
  failed += !restored;
  for (uint32_t slot = 0; slot < 4; ++slot) {
    const uint32_t address = 0x01000000 + slot * 0x10000 + slot * 0x1000;
    if (!heap->AllocFixed(address, 0x1000, 0x1000,
        xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
        xe::kMemoryProtectRead | xe::kMemoryProtectWrite)) return 2;
    UploadCheck gpu(memory);
    if (!gpu.Initialize()) return 2;
    const bool uploaded = gpu.RequestRange(address, 0x1000);
    const bool valid = uploaded && gpu.IsRangeValid(address, 0x1000);
    const bool exact = gpu.uploaded_bytes == 0x1000;
    const uint32_t before = gpu.uploaded_bytes;
    const bool cached = uploaded && gpu.RequestRange(address, 0x1000) &&
                        before == gpu.uploaded_bytes;
    gpu.MemoryInvalidationCallback(address & ~0x3FFFu, 0x4000, true);
    const bool invalidated = !gpu.IsRangeValid(address, 0x1000);
    const bool refreshed = uploaded && gpu.RequestRange(address, 0x1000) &&
                           gpu.uploaded_bytes == 0x2000;
    const bool pass = valid && exact && cached && invalidated && refreshed;
    std::printf("GPU upload subpage %u: %s (upload=%d exact=%d cache=%d invalidate=%d refresh=%d)\n",
                slot, pass ? "PASS" : "FAIL", uploaded, exact, cached, invalidated, refreshed);
    failed += !pass;
  }
  return failed ? 1 : 0;
}
}  // namespace xbox360ps5
