// SPDX-License-Identifier: MIT
// Which CPUs and priority a thread of the title has on the console
// (platform/ps5/thread_place.cpp).
#pragma once
#include <cstddef>
namespace xbox360ps5 {
// The CPU the calling thread is on now.
int CurrentCpu();
// A thread's CPUs as a mask (0 when the system does not say) and its priority
// (-1 likewise). `thread` is a pthread_t; null is the calling thread.
unsigned long long ThreadCpus(void* thread);
int ThreadPriority(void* thread);
// "cpu N, priority P, cpus 0xMASK" for the calling thread.
void DescribeThread(char* out, size_t size);
// Puts the calling thread where the current arrangement wants it: the GPU
// command thread (gpu) on its own core, every other thread off that core. A
// thread calls this when it starts; the GPU command thread calls it again as
// itself.
void PlaceCallingThread(bool gpu);
// The same for another thread (a pthread_t), after the arrangement changed.
void PlaceThread(void* thread);
// Whether the GPU command thread has a core to itself. Changing it moves the
// GPU command thread at once; the others move when PlaceThread is called for
// them.
void SetGpuCoreDedicated(bool on);
bool GpuCoreDedicated();
// How many placements the system accepted and refused.
void ThreadPlaceCounts(unsigned* given, unsigned* not_given);
}
