// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include "xenia/base/platform.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"

int main() {
  using xe::cpu::ppc::PPCOpcode;
  struct Case { unsigned instruction; PPCOpcode expected; const char* name; };
  const Case cases[] = {
      {0x3860002a, PPCOpcode::addi, "li r3,42"},
      {0x4e800020, PPCOpcode::bclrx, "blr"},
      {0x48000004, PPCOpcode::bx, "b +4"},
      {0x80640000, PPCOpcode::lwz, "lwz r3,0(r4)"},
      {0x90640000, PPCOpcode::stw, "stw r3,0(r4)"},
      {0x60000000, PPCOpcode::ori, "nop"},
      {0x10000000, PPCOpcode::vaddubm, "VMX vaddubm"},
      {0x14000310, PPCOpcode::vxor128, "Xenon VMX128 vxor128"},
      {0x00000000, PPCOpcode::kInvalid, "invalid instruction"},
  };
  unsigned failures = 0;
  for (const auto& test : cases) {
    const bool ok = xe::cpu::ppc::LookupOpcode(test.instruction) == test.expected;
    std::printf("%s %08x %s\n", ok ? "PASS" : "FAIL", test.instruction, test.name);
    failures += !ok;
  }
  xe::StringBuffer disassembly;
  const bool decoded = xe::cpu::ppc::DisasmPPC(0x82000000, 0x3860002a, &disassembly);
  const bool correct = decoded && std::strstr(disassembly.buffer(), "r3") &&
                       std::strstr(disassembly.buffer(), "0x2A");
  std::printf("%s upstream disassembly: %s\n", correct ? "PASS" : "FAIL", disassembly.buffer());
  failures += !correct;
#if defined(__PROSPERO__)
#if !XE_PLATFORM_PS5 || XE_PLATFORM_LINUX || XE_PLATFORM_WIN32
#error PS5 must have its own platform identity
#endif
  std::puts("Target: native PS5 x86-64; no JIT/GPU probe was run");
#else
  std::puts("Target: host test; this does not validate PS5 execution");
#endif
  std::printf("Decoder cases: %zu; failures: %u\n", sizeof(cases)/sizeof(cases[0]), failures);
  return failures ? 1 : 0;
}
