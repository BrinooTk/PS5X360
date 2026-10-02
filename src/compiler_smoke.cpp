// SPDX-License-Identifier: MIT
#include <cstdio>
#include <memory>
#include "xenia/cpu/compiler/compiler.h"
#include "xenia/cpu/compiler/passes/simplification_pass.h"
#include "xenia/cpu/compiler/passes/dead_code_elimination_pass.h"
#include "xenia/cpu/compiler/passes/validation_pass.h"
#include "xenia/cpu/hir/hir_builder.h"

using namespace xe::cpu::hir;
namespace passes = xe::cpu::compiler::passes;

unsigned count(HIRBuilder& builder, const OpcodeInfo* opcode) {
  unsigned result = 0;
  for (auto* block = builder.first_block(); block; block = block->next)
    for (auto* i = block->instr_head; i; i = i->next)
      result += i->opcode == opcode;
  return result;
}

int main() {
  unsigned cases = 0, failures = 0;
  auto check = [&](const char* name, bool pass) {
    ++cases;
    failures += !pass;
    std::printf("%s %s\n", pass ? "PASS" : "FAIL", name);
  };
  // These passes do not access a Processor. Memory/MMIO/context promotion
  // passes are deliberately excluded from this isolated optimizer test.
  xe::cpu::compiler::Compiler compiler(nullptr);
  compiler.AddPass(std::make_unique<passes::ValidationPass>());
  compiler.AddPass(std::make_unique<passes::SimplificationPass>());
  compiler.AddPass(std::make_unique<passes::DeadCodeEliminationPass>());
  compiler.AddPass(std::make_unique<passes::ValidationPass>());
  for (unsigned round = 0; round < 3; ++round) {
    HIRBuilder builder;
    auto* source = builder.LoadContext(0, INT32_TYPE);
    auto* twice = builder.ByteSwap(builder.ByteSwap(source));
    builder.StoreContext(8, twice);
    builder.Mul(source, builder.LoadConstantInt32(17)); // unused result
    builder.Return();
    check("finalize synthetic HIR", builder.Finalize());
    check("test contains redundant byte swaps", count(builder, &OPCODE_BYTE_SWAP_info) == 2);
    check("test contains unused multiply", count(builder, &OPCODE_MUL_info) == 1);
    check("optimizer pipeline succeeds", compiler.Compile(&builder));
    check("redundant swaps removed", count(builder, &OPCODE_BYTE_SWAP_info) == 0);
    check("unused arithmetic removed", count(builder, &OPCODE_MUL_info) == 0);
    check("observable register store preserved", count(builder, &OPCODE_STORE_CONTEXT_info) == 1);
    bool original_stored = false;
    for (auto* block = builder.first_block(); block; block = block->next)
      for (auto* i = block->instr_head; i; i = i->next)
        if (i->opcode == &OPCODE_STORE_CONTEXT_info) original_stored = i->src2.value == source;
    check("stored operand retains original meaning", original_stored);
  }
  std::printf("COMPILER CASES %u FAILURES %u (no guest execution)\n", cases, failures);
  return failures ? 1 : 0;
}
