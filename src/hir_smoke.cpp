// SPDX-License-Identifier: MIT
#include <cstdint>
#include <cstdio>
#include "xenia/base/arena.h"
#include "xenia/cpu/hir/value.h"
#include "xenia/cpu/hir/block.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/hir/hir_builder.h"

using xe::cpu::hir::Value;
using namespace xe::cpu::hir;

int main() {
  unsigned cases = 0, failures = 0;
  auto check = [&](const char* name, bool passed) {
    ++cases;
    if (!passed) ++failures;
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
  };
  Value a{}, b{};
  a.set_constant(std::uint32_t(0xffffffff));
  b.set_constant(std::uint32_t(1));
  a.Add(&b);
  check("32-bit addition wraps", a.constant.u32 == 0);
  a.set_constant(std::int8_t(-2));
  a.SignExtend(INT64_TYPE);
  check("signed extension", a.constant.i64 == -2);
  a.set_constant(std::uint8_t(254));
  a.ZeroExtend(INT64_TYPE);
  check("unsigned extension", a.constant.u64 == 254);
  a.set_constant(std::uint32_t(0x12345678));
  a.ByteSwap();
  check("guest byte order", a.constant.u32 == 0x78563412);
  a.set_constant(std::int32_t(-16));
  b.set_constant(std::uint8_t(2));
  a.Sha(&b);
  check("arithmetic right shift", a.constant.i32 == -4);
  a.set_constant(std::uint32_t(0xf0000000));
  a.Shr(&b);
  check("logical right shift", a.constant.u32 == 0x3c000000);
  a.set_constant(std::int32_t(-1));
  b.set_constant(std::int32_t(1));
  check("signed comparison", a.IsConstantSLT(&b));
  check("unsigned comparison", !a.IsConstantULT(&b));
  a.set_constant(1.5f);
  b.set_constant(2.0f);
  a.Mul(&b);
  check("float arithmetic", a.constant.f32 == 3.0f);
  xe::vec128_t left{}, right{};
  left.low = 0x123456789abcdef0ull;
  left.high = 0xfedcba9876543210ull;
  right.low = 0xffffffffffffffffull;
  right.high = 0xffffffffffffffffull;
  a.set_constant(left);
  b.set_constant(right);
  a.Xor(&b);
  check("128-bit vector xor", a.constant.v128.low == ~left.low &&
        a.constant.v128.high == ~left.high);
  xe::Arena arena(64 * 1024);
  auto* first = arena.Alloc<Value>();
  check("HIR value alignment", reinterpret_cast<std::uintptr_t>(first) % alignof(Value) == 0);
  for (unsigned i = 0; i < 2000; ++i) arena.Alloc<Value>();
  arena.Reset();
  check("arena reuse across translation blocks", arena.Alloc<Value>() == first);
  Block block{};
  block.arena = &arena;
  Instr first_instr{}, second_instr{};
  first_instr.block = second_instr.block = &block;
  block.instr_head = &first_instr;
  block.instr_tail = &second_instr;
  first_instr.next = &second_instr;
  second_instr.prev = &first_instr;
  a = Value{};
  b = Value{};
  first_instr.set_src1(&a);
  second_instr.set_src1(&a);
  check("HIR tracks multiple consumers", a.use_head && a.use_head->next &&
        a.use_head->instr == &second_instr);
  second_instr.set_src1(&b);
  check("operand replacement updates use chains", a.use_head && !a.use_head->next &&
        a.use_head->instr == &first_instr && b.use_head->instr == &second_instr);
  first_instr.Remove();
  check("instruction removal updates block and uses", !a.use_head &&
        block.instr_head == &second_instr && !second_instr.prev);
  second_instr.Remove();
  check("empty block after cleanup", !b.use_head && !block.instr_head && !block.instr_tail);
  HIRBuilder builder;
  auto* sum = builder.Add(builder.LoadConstantInt64(40), builder.LoadConstantInt64(2));
  check("builder folds constant expression", sum->IsConstant() && sum->constant.i64 == 42);
  auto* reg = builder.LoadContext(0, INT64_TYPE);
  auto* result = builder.Add(reg, sum);
  builder.StoreContext(8, result);
  builder.Return();
  check("builder finalizes register computation", builder.Finalize());
  xe::StringBuffer dump;
  builder.Dump(&dump);
  check("translation contains runtime add", dump.to_string().find("add") != std::string::npos);
  builder.Reset();
  check("translation reset discards previous blocks", !builder.first_block());
  std::printf("HIR CASES %u FAILURES %u (no guest execution)\n", cases, failures);
  return failures ? 1 : 0;
}
