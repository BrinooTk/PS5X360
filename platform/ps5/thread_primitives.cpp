// SPDX-License-Identifier: MIT
// Primitive services needed by the CPU runtime. This does not implement
// guest scheduling, Thread, wait handles or user callback delivery.
#include "xenia/base/threading.h"
#include <cerrno>
#include <ctime>
#include <pthread.h>
#include <sched.h>
#if XE_PLATFORM_PS5
#include <pthread_np.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace xe::threading {
uint32_t current_thread_system_id() {
#if XE_PLATFORM_PS5
  return static_cast<uint32_t>(pthread_getthreadid_np());
#else
  return static_cast<uint32_t>(syscall(SYS_gettid));
#endif
}
void MaybeYield() { sched_yield(); }
void SyncMemory() { std::atomic_thread_fence(std::memory_order_seq_cst); }
void Sleep(std::chrono::microseconds duration) {
  if (duration.count() <= 0) return;
  timespec remaining{duration.count() / 1000000, (duration.count() % 1000000) * 1000};
  while (nanosleep(&remaining, &remaining) == -1 && errno == EINTR) {}
}
TlsHandle AllocateTlsHandle() {
  pthread_key_t key;
  if (pthread_key_create(&key, nullptr)) return static_cast<TlsHandle>(-1);
  return static_cast<TlsHandle>(key);
}
bool FreeTlsHandle(TlsHandle key) { return pthread_key_delete(static_cast<pthread_key_t>(key)) == 0; }
uintptr_t GetTlsValue(TlsHandle key) { return reinterpret_cast<uintptr_t>(pthread_getspecific(static_cast<pthread_key_t>(key))); }
bool SetTlsValue(TlsHandle key, uintptr_t value) {
  return pthread_setspecific(static_cast<pthread_key_t>(key), reinterpret_cast<void*>(value)) == 0;
}
}
