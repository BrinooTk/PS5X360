// SPDX-License-Identifier: MIT
#include <cstdio>
#include <thread>
#include <vector>
#include "xenia/base/atomic.h"

int main() {
  unsigned cases = 0, failures = 0;
  auto check = [&](const char* name, bool pass) {
    ++cases;
    failures += !pass;
    std::printf("%s %s\n", pass ? "PASS" : "FAIL", name);
  };
  volatile int32_t a = 0;
  volatile int64_t b = 0;
  check("increment returns new value", xe::atomic_inc(&a) == 1);
  check("decrement returns new value", xe::atomic_dec(&a) == 0);
  check("32-bit exchange returns previous value", xe::atomic_exchange(int32_t(7), &a) == 0 && a == 7);
  check("32-bit fetch-add returns previous value", xe::atomic_exchange_add(int32_t(3), &a) == 7 && a == 10);
  check("failed CAS leaves value intact", !xe::atomic_cas(int32_t(9), int32_t(4), &a) && a == 10);
  check("successful CAS stores new value", xe::atomic_cas(int32_t(10), int32_t(4), &a) && a == 4);
  constexpr int64_t wide = 0x123456789abcdef;
  check("64-bit exchange", xe::atomic_exchange(wide, &b) == 0 && b == wide);
  check("64-bit fetch-add", xe::atomic_exchange_add(int64_t(1), &b) == wide && b == wide + 1);
  check("64-bit CAS failure", !xe::atomic_cas(wide, int64_t(0), &b) && b == wide + 1);
  check("64-bit CAS success", xe::atomic_cas(wide + 1, int64_t(0), &b) && b == 0);
  volatile uint32_t u = 0xffffffffu;
  check("unsigned wrapper wraps at 32 bits", xe::atomic_inc(&u) == 0 && u == 0);
  a = 0;
  std::vector<std::thread> workers;
  for (unsigned i = 0; i < 4; ++i) {
    workers.emplace_back([&] {
      for (unsigned n = 0; n < 25000; ++n) {
        xe::atomic_inc(&a);
        xe::atomic_exchange_add(int64_t(1), &b);
      }
    });
  }
  for (auto& worker : workers) worker.join();
  check("concurrent 32-bit updates", a == 100000);
  check("concurrent 64-bit updates", b == 100000);
  std::printf("ATOMIC CASES %u FAILURES %u\n", cases, failures);
  return failures ? 1 : 0;
}
