// SPDX-License-Identifier: MIT
// Hard link gate for actual Memory / Processor / x64 backend. Undefined
// platform services must fail the link; never generate a stubbed executable.
#include "xenia/memory.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/backend/x64/x64_backend.h"
#include "xenia/cpu/raw_module.h"
#include "xenia/cpu/thread_state.h"
#include <cstdio>
#include <array>
namespace {
struct NativeState { unsigned calls{}; bool arguments{}; };
NativeState state;
void native_contract(xe::cpu::ppc::PPCContext* ctx, xe::kernel::KernelState* kernel) {
  state.arguments = ctx && kernel == nullptr;
  ++state.calls;
  ctx->r[3] = ctx->r[4] + 19;
}
}
int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  // Force the vector fallback used by Zen 2 (no GFNI/AVX512 shortcuts).
  cvars::x64_extension_mask = 127;
  std::fprintf(stderr, "Runtime gate: memory initialization\n");
  xe::Memory memory;
  if (!memory.Initialize()) { std::fputs("Guest memory initialization failed\n", stderr); return 1; }
  xe::cpu::ExportResolver exports;
  std::fprintf(stderr, "Runtime gate: processor setup\n");
  xe::cpu::Processor processor(&memory, &exports);
  if (!processor.Setup(std::make_unique<xe::cpu::backend::x64::X64Backend>())) {
    std::fputs("CPU runtime initialization failed\n", stderr); return 1;
  }
  constexpr uint32_t code_address = 0x80000000, stack_address = 0x10000000;
  constexpr uint32_t program[] = {0x3860fffe, 0x38830007, 0x60858000,
      0x3ce01000, 0x90a70020, 0x80c70020, 0x4e800020};
  const auto flags = xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit;
  std::fprintf(stderr, "Runtime gate: guest code and stack allocation\n");
  const auto access = xe::kMemoryProtectRead | xe::kMemoryProtectWrite;
  if (!memory.LookupHeap(code_address)->AllocFixed(code_address, 0x10000, 0, flags, access) ||
      !memory.LookupHeap(stack_address)->AllocFixed(stack_address, 0x20000, 0, flags, access)) return 2;
  for (unsigned n = 0; n < std::size(program); ++n)
    xe::store_and_swap<uint32_t>(memory.TranslateVirtual(code_address + n * 4), program[n]);
  // Same synthetic import-thunk convention as XexModule: sc 2 followed by blr.
  xe::store_and_swap<uint32_t>(memory.TranslateVirtual(code_address + 0x500), 0x44000042);
  xe::store_and_swap<uint32_t>(memory.TranslateVirtual(code_address + 0x504), 0x4e800020);
  auto module = std::make_unique<xe::cpu::RawModule>(&processor);
  module->set_name("synthetic-runtime-contract"); module->set_executable(true);
  module->SetAddressRange(code_address, 0x10000);
  xe::cpu::Function* native_symbol = nullptr;
  module->DeclareFunction(code_address + 0x500, &native_symbol);
  if (!native_symbol) return 6;
  static_cast<xe::cpu::GuestFunction*>(native_symbol)->SetupExtern(native_contract);
  native_symbol->set_end_address(code_address + 0x504);
  native_symbol->set_status(xe::cpu::Symbol::Status::kDeclared);
  if (!processor.AddModule(std::move(module))) return 3;
  xe::cpu::ThreadState thread(&processor, 1, stack_address + 0x1fff0);
  std::fprintf(stderr, "Runtime gate: synthetic PPC execution\n");
  if (!processor.Execute(&thread, code_address)) return 4;
  const auto* ctx = thread.context();
  const bool pass = ctx->r[3] == uint64_t(-2) && ctx->r[4] == 5 &&
      ctx->r[5] == 0x8005 && ctx->r[6] == 0x8005 &&
      xe::load_and_swap<uint32_t>(memory.TranslateVirtual(stack_address + 32)) == 0x8005;
  std::printf("Actual PPC -> x64 JIT execution: %s; no game executed\n", pass ? "PASS" : "FAIL");
  if (!pass) return 5;
  unsigned cases = 1, failures = 0;
  auto check = [&](const char* name, bool result) {
    ++cases; failures += !result;
    std::printf("%s %s\n", result ? "PASS" : "FAIL", name);
  };
  auto write = [&](uint32_t offset, const auto& words) {
    for (unsigned n = 0; n < std::size(words); ++n)
      xe::store_and_swap<uint32_t>(memory.TranslateVirtual(code_address + offset + n * 4), words[n]);
  };
  const uint32_t native_program[] = {
      0x7fc802a6, 0x38800017, // save LR; r4 = 23
      0x3d808000u,
      0x618c0500u,
      0x7d8903a6, 0x4e800421, // mtctr r12; bctrl
      0x7fc803a6, 0x38a30001, 0x4e800020}; // restore LR; r5 = r3 + 1; return
  write(0x100, native_program);
  std::fprintf(stderr, "Runtime gate: native ABI calls\n");
  for (unsigned n = 0; n < 32; ++n) {
    check("translated PPC calls host handler and continues",
        processor.Execute(&thread, code_address + 0x100) && state.arguments &&
        thread.context()->r[3] == 42 && thread.context()->r[5] == 43 && state.calls == n + 1);
  }
  const uint32_t nested_program[] = {0x7fc802a6, 0x3d808000, 0x618c0300,
      0x7d8903a6, 0x4e800421, 0x7fc803a6, 0x38830001, 0x4e800020};
  const uint32_t sub_program[] = {0x38600055, 0x4e800020};
  write(0x200, nested_program); write(0x300, sub_program);
  std::fprintf(stderr, "Runtime gate: unresolved guest function call\n");
  check("lazy function resolver and nested guest return",
      processor.Execute(&thread, code_address + 0x200) &&
      thread.context()->r[3] == 85 && thread.context()->r[4] == 86);
  const uint32_t vector_program[] = {0x10632104, 0x4e800020}; // vslb v3,v3,v4
  write(0x400, vector_program);
  std::fprintf(stderr, "Runtime gate: native vector fallback\n");
  for (unsigned shift = 0; shift < 8; ++shift) {
    for (auto& byte : thread.context()->v[3].u8) byte = 0xff;
    for (auto& byte : thread.context()->v[4].u8) byte = shift;
    bool correct = processor.Execute(&thread, code_address + 0x400);
    for (auto byte : thread.context()->v[3].u8) correct &= byte == uint8_t(0xff << shift);
    check("VMX128 shift uses stashed vector arguments", correct);
  }
  // Keep fifteen computed vectors live across the host helper. This exercises
  // preservation beyond XMM1..5, which System V callees may also overwrite.
  std::array<uint32_t, 32> live_program{};
  unsigned pos = 0;
  for (unsigned reg = 8; reg < 23; ++reg)
    live_program[pos++] = 0x10000000 | (reg << 21) | (reg << 16) | (31 << 11);
  live_program[pos++] = 0x10632104;
  for (unsigned reg = 8; reg < 23; ++reg)
    live_program[pos++] = 0x10000000 | (3 << 21) | (3 << 16) | (reg << 11);
  live_program[pos++] = 0x4e800020;
  write(0x600, live_program);
  for (unsigned shift = 0; shift < 8; ++shift) {
    for (auto& byte : thread.context()->v[3].u8) byte = 0xff;
    for (auto& byte : thread.context()->v[4].u8) byte = shift;
    for (auto& byte : thread.context()->v[31].u8) byte = 1;
    for (unsigned reg = 8; reg < 23; ++reg)
      for (auto& byte : thread.context()->v[reg].u8) byte = reg - 7;
    bool correct = processor.Execute(&thread, code_address + 0x600);
    const auto expected = uint8_t((0xff << shift) + 135);
    for (auto byte : thread.context()->v[3].u8) correct &= byte == expected;
    for (unsigned reg = 8; reg < 23; ++reg)
      for (auto byte : thread.context()->v[reg].u8) correct &= byte == reg - 6;
    check("fifteen live vectors survive the System V helper", correct);
  }
  std::printf("JIT RUNTIME CASES %u FAILURES %u (synthetic programs, no game)\n", cases, failures);
  if (failures) return 7;
  return 0;
}
