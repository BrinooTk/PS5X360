// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include "third_party/xbyak/xbyak/xbyak.h"
#include <capstone/capstone.h>

int main() {
  unsigned cases = 0, failures = 0;
  auto check = [&](const char* name, bool pass) {
    ++cases;
    failures += !pass;
    std::printf("%s %s\n", pass ? "PASS" : "FAIL", name);
  };
  // Caller-owned data buffer: no executable allocation, no function call to
  // generated code. This validates encoding/decoding, not the Xenia JIT.
  unsigned char bytes[256]{};
  Xbyak::CodeGenerator code(sizeof(bytes), bytes);
  code.mov(code.eax, 42);
  code.add(code.eax, 7);
  code.bswap(code.eax);
  code.ret();
  code.ready();
  check("generator uses caller buffer", code.getCode() == bytes);
  check("32-bit immediate encoding", code.getSize() >= 5 && bytes[0] == 0xb8 && bytes[1] == 42 && bytes[2] == 0);
  csh decoder{};
  if (cs_open(CS_ARCH_X86, CS_MODE_64, &decoder) != CS_ERR_OK) return 1;
  cs_option(decoder, CS_OPT_DETAIL, CS_OPT_ON);
  cs_insn* instructions = nullptr;
  auto count = cs_disasm(decoder, bytes, code.getSize(), 0x1000, 0, &instructions);
  check("all four instructions decoded", count == 4);
  if (count == 4) {
    check("immediate semantics", instructions[0].id == X86_INS_MOV &&
          instructions[0].detail && instructions[0].detail->x86.operands[1].type == X86_OP_IMM &&
          instructions[0].detail->x86.operands[1].imm == 42);
    check("arithmetic instruction", instructions[1].id == X86_INS_ADD);
    check("byte order instruction", instructions[2].id == X86_INS_BSWAP);
    check("return instruction", instructions[3].id == X86_INS_RET);
  } else {
    failures += 4;
  }
  cs_free(instructions, count);
  code.reset();
  Xbyak::Label target;
  code.jmp(target);
  code.nop();
  code.L(target);
  code.ret();
  code.ready();
  instructions = nullptr;
  count = cs_disasm(decoder, bytes, code.getSize(), 0x1000, 0, &instructions);
  check("forward label resolves", count == 3 && instructions[0].id == X86_INS_JMP &&
        instructions[0].detail && instructions[0].detail->x86.operands[0].type == X86_OP_IMM &&
        instructions[0].detail->x86.operands[0].imm == int64_t(instructions[2].address));
  cs_free(instructions, count);
  cs_close(&decoder);
  std::printf("CODEGEN CASES %u FAILURES %u (encoding only)\n", cases, failures);
  return failures ? 1 : 0;
}
