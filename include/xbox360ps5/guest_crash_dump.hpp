// SPDX-License-Identifier: MIT
// Guest state for a crash in translated code: registers, the code around the
// return address, the stack frame and what the pointer registers refer to.
#pragma once
#include <cstdio>
#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/memory.h"
namespace xbox360ps5 {
inline void DumpGuestWords(xe::Memory* memory, const char* name, uint32_t address, uint32_t bytes) {
  for (uint32_t at = address & ~15u; at < address + bytes; at += 16) {
    auto* host = memory->TranslateVirtual(at);
    size_t length = 16;
    xe::memory::PageAccess access;
    if (!xe::memory::QueryProtect(host, length, access) || access == xe::memory::PageAccess::kNoAccess) {
      XELOGE("{} {:08X}: not readable", name, at);
      return;
    }
    auto* words = reinterpret_cast<const xe::be<uint32_t>*>(host);
    XELOGE("{} {:08X}: {:08X} {:08X} {:08X} {:08X}", name, at, uint32_t(words[0]),
           uint32_t(words[1]), uint32_t(words[2]), uint32_t(words[3]));
  }
}
inline void DumpGuestCrash(xe::Memory* memory, xe::cpu::ppc::PPCContext* context, uint32_t thread_id) {
  XELOGE("Guest thread {} LR {:08X} CTR {:08X}", thread_id, uint32_t(context->lr), uint32_t(context->ctr));
  for (int i = 0; i < 32; i += 4) {
    XELOGE(" r{:<2} {:016X} {:016X} {:016X} {:016X}", i, context->r[i], context->r[i + 1],
           context->r[i + 2], context->r[i + 3]);
  }
  const uint32_t lr = uint32_t(context->lr);
  DumpGuestWords(memory, "code", lr - 0x200, 0x280);
  DumpGuestWords(memory, "stack", uint32_t(context->r[1]), 0x100);
  for (int i = 3; i < 32; ++i) {
    const uint32_t value = uint32_t(context->r[i]);
    if (i > 12 && i < 24) continue;
    if (value < 0x10000 || (value >= 0x70000000 && value < 0x80000000)) continue;
    char name[8];
    std::snprintf(name, sizeof(name), "r%d", i);
    DumpGuestWords(memory, name, value & ~15u, 0x60);
  }
}
}
