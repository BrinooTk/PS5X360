// SPDX-License-Identifier: MIT
#pragma once
#include "xenia/cpu/ppc/ppc_opcode_info.h"

namespace xbox360ps5 {
using xe::cpu::ppc::PPCOpcode;
struct DecoderCase { unsigned instruction; PPCOpcode expected; const char* name; };
inline constexpr DecoderCase decoder_cases[] = {
    {0x3860002a, PPCOpcode::addi, "LI R3 42"},
    {0x4e800020, PPCOpcode::bclrx, "BLR"},
    {0x48000004, PPCOpcode::bx, "BRANCH"},
    {0x80640000, PPCOpcode::lwz, "LOAD WORD"},
    {0x90640000, PPCOpcode::stw, "STORE WORD"},
    {0x60000000, PPCOpcode::ori, "NOP"},
    {0x10000000, PPCOpcode::vaddubm, "VMX ADD"},
    {0x14000310, PPCOpcode::vxor128, "VMX128 XOR"},
    {0x00000000, PPCOpcode::kInvalid, "INVALID OPCODE"},
};
inline constexpr unsigned decoder_case_count = sizeof(decoder_cases) / sizeof(decoder_cases[0]);
inline bool check(const DecoderCase& test) {
  return xe::cpu::ppc::LookupOpcode(test.instruction) == test.expected;
}
}  // namespace xbox360ps5
