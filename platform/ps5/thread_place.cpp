// SPDX-License-Identifier: MIT
// Which CPUs a thread of the title runs on. On the console a thread starts with
// the CPUs of the thread that made it. Measured on firmware 13.60: the title's
// first thread has CPUs 0 to 12 (mask 0x1fff) at priority 700, every thread of
// the emulator inherits them, and in five seconds each busy thread was seen on
// all thirteen: the system moves them about freely, and two threads can share
// the two halves of one core.
//
// The experiment here gives the GPU command thread, which the guest waits for,
// a core to itself: it stays on CPU 2, and no other thread of the title may use
// CPU 2 or its other half, CPU 3 (CPUs come in pairs, 0-1, 2-3, ...; the first
// pair is left alone because the system's own work lands there).
#include "xbox360ps5/thread_place.hpp"
#include <atomic>
#include <cstdio>
#include <pthread.h>
extern "C" {
int scePthreadGetaffinity(pthread_t, unsigned long long*);
int scePthreadSetaffinity(pthread_t, unsigned long long);
int scePthreadGetprio(pthread_t, int*);
int sceKernelGetCurrentCpu(void);
}
namespace xbox360ps5 {
namespace {
constexpr unsigned long long kTitleCpus = 0x1fffull;  // CPUs 0 to 12.
constexpr unsigned long long kGpuCore = 0xcull;       // CPUs 2 and 3: one core.
constexpr unsigned long long kGpuCpu = 0x4ull;        // CPU 2.
// Off: experimental.10 measured no difference with it on (24 to 26 frames a
// second at five switches in one scene).
std::atomic<bool> dedicated{false};
std::atomic<pthread_t> gpu_thread{nullptr};
std::atomic<unsigned> placed{0}, refused{0};
pthread_t Of(void* thread) { return thread ? static_cast<pthread_t>(thread) : pthread_self(); }
void Place(pthread_t thread, bool gpu) {
  const bool own_core = dedicated.load(std::memory_order_relaxed);
  const unsigned long long wanted = !own_core ? kTitleCpus : gpu ? kGpuCpu : kTitleCpus & ~kGpuCore;
  unsigned long long have = 0;
  if (scePthreadGetaffinity(thread, &have) == 0 && have == wanted) return;
  if (scePthreadSetaffinity(thread, wanted) == 0) ++placed; else ++refused;
}
}
int CurrentCpu() { return sceKernelGetCurrentCpu(); }
unsigned long long ThreadCpus(void* thread) {
  unsigned long long mask = 0;
  return scePthreadGetaffinity(Of(thread), &mask) == 0 ? mask : 0;
}
int ThreadPriority(void* thread) {
  int priority = -1;
  return scePthreadGetprio(Of(thread), &priority) == 0 ? priority : -1;
}
void DescribeThread(char* out, size_t size) {
  std::snprintf(out, size, "cpu %d, priority %d, cpus 0x%llx", CurrentCpu(), ThreadPriority(nullptr), ThreadCpus(nullptr));
}
void PlaceCallingThread(bool gpu) {
  if (gpu) gpu_thread = pthread_self();
  Place(pthread_self(), gpu);
}
void PlaceThread(void* thread) {
  if (!thread) return;
  const pthread_t handle = static_cast<pthread_t>(thread);
  Place(handle, handle == gpu_thread.load());
}
void SetGpuCoreDedicated(bool on) {
  dedicated = on;
  if (const pthread_t gpu = gpu_thread.load()) Place(gpu, true);
}
bool GpuCoreDedicated() { return dedicated; }
void ThreadPlaceCounts(unsigned* given, unsigned* not_given) {
  *given = placed; *not_given = refused;
}
}
