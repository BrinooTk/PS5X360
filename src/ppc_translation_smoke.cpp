// SPDX-License-Identifier: MIT
#include <cstdio>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include "xenia/cpu/compiler/compiler.h"
#include "xenia/cpu/compiler/passes/simplification_pass.h"
#include "xenia/cpu/compiler/passes/dead_code_elimination_pass.h"
#include "xenia/cpu/ppc/ppc_hir_builder.h"
#include "xenia/cpu/ppc/ppc_decode_data.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"

namespace xe::cpu::ppc {
int InstrEmit_addi(PPCHIRBuilder&, const InstrData&);
int InstrEmit_ori(PPCHIRBuilder&, const InstrData&);
int InstrEmit_lwz(PPCHIRBuilder&, const InstrData&);
int InstrEmit_stw(PPCHIRBuilder&, const InstrData&);
}

using namespace xe::cpu::ppc;
using namespace xe::cpu::hir;

struct ProbeState {
  std::array<unsigned char, sizeof(PPCContext)> context{};
  std::array<unsigned char, 256> memory{};
  uint64_t reg(unsigned index) const {
    uint64_t result;
    std::memcpy(&result, context.data() + offsetof(PPCContext, r) + index * 8, 8);
    return result;
  }
};

// Test oracle for this small integer HIR subset, not a production interpreter.
// Unknown operations fail instead of being skipped or silently approximated.
ProbeState evaluate(HIRBuilder& builder) {
  ProbeState state;
  std::map<Value*, uint64_t> values;
  auto value = [&](Value* v) -> uint64_t {
    if (v->IsConstant()) return v->AsUint64();
    return values.at(v);
  };
  auto read = [](const auto& bytes, uint64_t address, size_t width) {
    if (address > bytes.size() || width > bytes.size() - address)
      throw std::out_of_range("probe read");
    uint64_t result = 0;
    for (size_t n = 0; n < width; ++n) result |= uint64_t(bytes[address + n]) << (8 * n);
    return result;
  };
  auto write = [](auto& bytes, uint64_t address, uint64_t v, size_t width) {
    if (address > bytes.size() || width > bytes.size() - address)
      throw std::out_of_range("probe write");
    for (size_t n = 0; n < width; ++n) bytes[address + n] = static_cast<unsigned char>(v >> (8 * n));
  };
  for (auto* block = builder.first_block(); block; block = block->next) {
    for (auto* i = block->instr_head; i; i = i->next) {
      uint64_t result = 0;
      switch (i->opcode->num) {
        case OPCODE_NOP: case OPCODE_SOURCE_OFFSET: continue;
        case OPCODE_RETURN: return state;
        case OPCODE_LOAD_CONTEXT:
          result = read(state.context, i->src1.offset, GetTypeSize(i->dest->type)); break;
        case OPCODE_STORE_CONTEXT:
          write(state.context, i->src1.offset, value(i->src2.value), GetTypeSize(i->src2.value->type)); continue;
        case OPCODE_ADD: result = value(i->src1.value) + value(i->src2.value); break;
        case OPCODE_OR: result = value(i->src1.value) | value(i->src2.value); break;
        case OPCODE_ASSIGN: case OPCODE_ZERO_EXTEND: case OPCODE_TRUNCATE:
          result = value(i->src1.value); break;
        case OPCODE_BYTE_SWAP:
          result = value(i->src1.value);
          if (i->dest->type == INT32_TYPE) result = __builtin_bswap32(uint32_t(result));
          else if (i->dest->type == INT64_TYPE) result = __builtin_bswap64(result);
          else throw std::runtime_error("unsupported probe byte swap");
          break;
        case OPCODE_LOAD_OFFSET:
          result = read(state.memory, value(i->src1.value) + value(i->src2.value), GetTypeSize(i->dest->type)); break;
        case OPCODE_STORE_OFFSET:
          write(state.memory, value(i->src1.value) + value(i->src2.value), value(i->src3.value), GetTypeSize(i->src3.value->type)); continue;
        default: throw std::runtime_error(std::string("unsupported probe opcode: ") + i->opcode->name);
      }
      const auto width = GetTypeSize(i->dest->type);
      if (width < 8) result &= (uint64_t(1) << (width * 8)) - 1;
      values[i->dest] = result;
    }
  }
  return state;
}

bool emit(PPCHIRBuilder& builder, uint32_t word) {
  builder.BeginProbeInstruction();
  InstrData instruction{};
  instruction.code = word;
  instruction.opcode = LookupOpcode(word);
  instruction.opcode_info = &GetOpcodeInfo(instruction.opcode);
  switch (instruction.opcode) {
    case PPCOpcode::addi: return !InstrEmit_addi(builder, instruction);
    case PPCOpcode::ori: return !InstrEmit_ori(builder, instruction);
    case PPCOpcode::lwz: return !InstrEmit_lwz(builder, instruction);
    case PPCOpcode::stw: return !InstrEmit_stw(builder, instruction);
    default: return false;
  }
}

unsigned run_ppc_translation_probe(unsigned& case_count) {
  unsigned cases = 0, failures = 0;
  auto check = [&](const char* name, bool pass) {
    ++cases; failures += !pass;
    std::printf("%s %s\n", pass ? "PASS" : "FAIL", name);
  };
  try {
    PPCHIRBuilder builder(nullptr);
    builder.Reset();
    // Bytes come from a synthetic test program, no game or guest runtime.
    const uint32_t words[] = {
      0x38000063, // li r0,99
      0x3860fffe, // li r3,-2: RA=0 must ignore r0
      0x38830007, // addi r4,r3,7
      0x60858000, // ori r5,r4,0x8000
      0x90a00020, // stw r5,32(0)
      0x80c00020, // lwz r6,32(0)
      0x38e00030, // li r7,48
      0x90a7fff8, // stw r5,-8(r7)
      0x8107fff8, // lwz r8,-8(r7)
    };
    for (auto word : words) check("upstream PPC emitter accepts word", emit(builder, word));
    builder.Return();
    check("PPC-derived HIR finalizes", builder.Finalize());
    auto before = evaluate(builder);
    check("r0 written independently", before.reg(0) == 99);
    check("signed immediate and RA zero semantics", before.reg(3) == uint64_t(-2));
    check("dependent register arithmetic", before.reg(4) == 5);
    check("ORI immediate is unsigned", before.reg(5) == 0x8005);
    check("big-endian word in guest memory", before.memory[32] == 0 && before.memory[33] == 0 && before.memory[34] == 0x80 && before.memory[35] == 5);
    check("load restores word with zero extension", before.reg(6) == 0x8005);
    check("negative displacement addresses correct word", before.memory[42] == 0x80 && before.memory[43] == 5 && before.reg(8) == 0x8005);
    xe::cpu::compiler::Compiler compiler(nullptr);
    namespace passes = xe::cpu::compiler::passes;
    compiler.AddPass(std::make_unique<passes::SimplificationPass>());
    compiler.AddPass(std::make_unique<passes::DeadCodeEliminationPass>());
    check("real optimizer accepts PPC-derived HIR", compiler.Compile(&builder));
    auto after = evaluate(builder);
    check("optimized register state is identical", before.context == after.context);
    check("optimized guest memory is identical", before.memory == after.memory);
    builder.Reset();
    check("unsupported opcode rejected by probe", !emit(builder, 0));
    check("builder reused for new program", emit(builder, 0x3860002a));
    builder.Return();
    check("second program finalizes", builder.Finalize());
    check("second program produces 42", evaluate(builder).reg(3) == 42);
  } catch (const std::exception& error) {
    ++failures;
    std::fprintf(stderr, "FAIL translation oracle: %s\n", error.what());
  }
  std::printf("PPC TRANSLATION CASES %u FAILURES %u (HIR oracle, no JIT)\n", cases, failures);
  case_count = cases;
  return failures;
}

#ifndef XBOX360PS5_PROBE_EMBEDDED
int main() {
  unsigned cases = 0;
  return run_ppc_translation_probe(cases) ? 1 : 0;
}
#endif
