// SPDX-License-Identifier: MIT
// Preserve the native converter's allocation observation hook while the
// actual allocation operators come from the SDK's complete libc++ runtime.
extern "C" __attribute__((noinline, visibility("hidden"))) bool
ps5ObserveOwnedAllocation(const void* address) noexcept {
  __asm__ volatile("" : : "r"(address) : "memory");
  return address != nullptr;
}
