// SPDX-License-Identifier: MIT
// Xenia signal transport using the SDK's BSD ABI. Unknown signal-context
// layouts are rejected. Native ABI/recovery require hardware verification.
#include "xenia/base/exception_handler.h"
#include "xenia/base/host_thread_context.h"
#include "xenia/base/logging.h"
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
namespace xbox360ps5 { extern std::atomic<unsigned long long> fault_count; }
namespace xe {
namespace {
struct Entry {
  std::atomic<ExceptionHandler::Handler> handler{nullptr};
  std::atomic<void*> data{nullptr};
  std::atomic<unsigned long long> order{0};
};
std::array<Entry, 8> entries;
std::mutex administration;
unsigned long long next_order = 0;
bool installed = false;
struct sigaction old_segv{}, old_ill{};
static_assert(std::atomic<ExceptionHandler::Handler>::is_always_lock_free);
static_assert(std::atomic<void*>::is_always_lock_free);
static_assert(std::atomic<unsigned long long>::is_always_lock_free);
void forward(int number, siginfo_t* info, void* context) {
  const auto& old = number == SIGSEGV ? old_segv : old_ill;
  if (old.sa_handler == SIG_DFL || old.sa_handler == SIG_IGN) {
    struct sigaction normal{}; normal.sa_handler = SIG_DFL;
    sigemptyset(&normal.sa_mask);
    if (sigaction(number, &normal, nullptr)) _exit(128 + number);
    raise(number); return;
  }
  if (old.sa_flags & SA_SIGINFO) old.sa_sigaction(number, info, context);
  else old.sa_handler(number);
}
void callback(int number, siginfo_t* info, void* raw_context) {
  ++xbox360ps5::fault_count;
#if XE_PLATFORM_PS5
  // The console's ucontext has 48 bytes between uc_sigmask and uc_mcontext that
  // the upstream payload SDK header omits; the SDK fork's header has them.
  auto& mc = *reinterpret_cast<mcontext_t*>(static_cast<char*>(raw_context) + 64);
#else
  auto& mc = static_cast<ucontext_t*>(raw_context)->uc_mcontext;
#endif
#if XE_PLATFORM_PS5
  // The console's general registers follow FreeBSD's layout (measured), but
  // its context length need not equal this SDK's structure size.
  const bool known_layout = true;
#else
  const bool known_layout = mc.mc_len == sizeof(mcontext_t);
#endif
  if (!known_layout) {
    constexpr char message[] = "Xenia: unsupported native signal context ABI\n";
    (void)write(STDERR_FILENO, message, sizeof(message) - 1);
    forward(number, info, raw_context); return;
  }
  HostThreadContext host{};
  host.rip = mc.mc_rip; host.eflags = mc.mc_rflags;
  const std::array<decltype(&mc.mc_rax), 16> gprs = {
    &mc.mc_rax, &mc.mc_rcx, &mc.mc_rdx, &mc.mc_rbx, &mc.mc_rsp, &mc.mc_rbp,
    &mc.mc_rsi, &mc.mc_rdi, &mc.mc_r8, &mc.mc_r9, &mc.mc_r10, &mc.mc_r11,
    &mc.mc_r12, &mc.mc_r13, &mc.mc_r14, &mc.mc_r15};
  for (unsigned n = 0; n < gprs.size(); ++n) host.int_registers[n] = *gprs[n];
  // FXSAVE XMM0 starts at byte 160. Gate this layout on the SDK's format tag.
  const bool have_xmm = mc.mc_fpformat == _MC_FPFMT_XMM;
  auto* xmm = reinterpret_cast<unsigned char*>(mc.mc_fpstate) + 160;
  static_assert(sizeof(mc.mc_fpstate) >= 160 + sizeof(host.xmm_registers));
  if (have_xmm) std::memcpy(host.xmm_registers, xmm, sizeof(host.xmm_registers));
  Exception fault;
  if (number == SIGILL) fault.InitializeIllegalInstruction(&host);
  else fault.InitializeAccessViolation(&host, uintptr_t(info->si_addr),
      mc.mc_err & 2 ? Exception::AccessViolationOperation::kWrite : Exception::AccessViolationOperation::kRead);
  unsigned long long after = 0;
  for (unsigned pass = 0; pass < entries.size(); ++pass) {
    Entry* selected = nullptr; unsigned long long first = ~0ull;
    for (auto& entry : entries) {
      const auto order = entry.order.load(std::memory_order_acquire);
      if (order > after && order < first && entry.handler.load(std::memory_order_acquire)) {
        selected = &entry; first = order;
      }
    }
    if (!selected) break;
    after = first;
    auto handler = selected->handler.load(std::memory_order_acquire);
    void* data = selected->data.load(std::memory_order_acquire);
    if (!handler || selected->order.load(std::memory_order_acquire) != first) continue;
    if (!handler(&fault, data)) continue;
    if (fault.modified_xmm_registers() && !have_xmm) break;
    mc.mc_rip = host.rip; mc.mc_rflags = host.eflags;
    for (unsigned n = 0; n < gprs.size(); ++n)
      if (fault.modified_int_registers() & (1u << n)) *gprs[n] = host.int_registers[n];
    for (unsigned n = 0; n < 16; ++n)
      if (fault.modified_xmm_registers() & (1u << n))
        std::memcpy(xmm + n * sizeof(vec128_t), &host.xmm_registers[n], sizeof(vec128_t));
    return;
  }
  forward(number, info, raw_context);
}
}
void ExceptionHandler::Install(Handler handler, void* data) {
  std::lock_guard lock(administration);
  if (!handler) FatalError("Null native exception handler");
  if (!installed) {
    struct sigaction action{}; action.sa_sigaction = callback; action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSEGV, &action, &old_segv)) FatalError("Cannot install SIGSEGV transport");
    if (sigaction(SIGILL, &action, &old_ill)) {
      sigaction(SIGSEGV, &old_segv, nullptr); FatalError("Cannot install SIGILL transport");
    }
    installed = true;
  }
  for (auto& entry : entries) {
    if (!entry.handler.load(std::memory_order_acquire)) {
      entry.data.store(data, std::memory_order_relaxed);
      entry.order.store(++next_order, std::memory_order_relaxed);
      entry.handler.store(handler, std::memory_order_release); return;
    }
  }
  FatalError("Native exception handler capacity exhausted");
}
void ExceptionHandler::Uninstall(Handler handler, void* data) {
  // Caller must stop guest threads before releasing MMIO/backend objects.
  std::lock_guard lock(administration);
  for (auto& entry : entries)
    if (entry.handler.load(std::memory_order_acquire) == handler && entry.data.load() == data)
      entry.handler.store(nullptr, std::memory_order_release);
  for (auto& entry : entries) if (entry.handler.load()) return;
  if (installed) {
    sigaction(SIGSEGV, &old_segv, nullptr); sigaction(SIGILL, &old_ill, nullptr); installed = false;
  }
}
}
