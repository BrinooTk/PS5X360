// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include "xenia/base/platform.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"
#include "probe_cases.hpp"

int main() {
  unsigned failures = 0;
  for (const auto& test : xbox360ps5::decoder_cases) {
    const bool ok = xbox360ps5::check(test);
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
  std::printf("Decoder cases: %u; failures: %u\n", xbox360ps5::decoder_case_count, failures);
  return failures ? 1 : 0;
}
