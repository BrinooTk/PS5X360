"""Exercise the PS5 spinning mutexes of the core on the host, in both modes, under ThreadSanitizer
and AddressSanitizer. In Docker, ThreadSanitizer needs `--security-opt seccomp=unconfined`."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
header = (root / '.deps/xenia-canary/src/xenia/base/mutex.h').read_text()
source = (root / '.deps/xenia-canary/src/xenia/base/mutex.cc').read_text()
a = header.index('extern std::atomic<bool> ps5_spin_mutexes;')
classes = header[a:header.index('using xe_unlikely_mutex = xe_fast_mutex;', a)]
b = source.index('std::atomic<bool> ps5_spin_mutexes{false};')
implementation = source[b:source.index('#endif', b)]
out = root / 'build/ps5-mutex-check'
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <immintrin.h>
#include <mutex>
#include <pthread.h>
#include <sched.h>
#include <thread>
#include <time.h>
#include <vector>
#define XE_LIKELY(x) __builtin_expect(!!(x), 1)
namespace xe {
''' + classes + implementation + r'''
}
template <class Mutex, bool recursive>
long Hammer(int threads, int rounds) {
  Mutex mutex;
  long counter = 0;  // Only ever touched under the mutex: a race here is the mutex failing.
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t) pool.emplace_back([&, t] {
    for (int n = 0; n < rounds; ++n) {
      if (n % 7 == 3) { if (!mutex.try_lock()) { --n; continue; } }
      else mutex.lock();
      if (recursive && n % 3 == 0) {
        mutex.lock();
        assert(mutex.try_lock());
        ++counter;
        mutex.unlock();
        mutex.unlock();
      } else {
        ++counter;
      }
      // Now and then the holder keeps the lock past the spin phase.
      if ((n + t) % 4096 == 0) { timespec nap = {0, 300000}; nanosleep(&nap, nullptr); }
      mutex.unlock();
    }
  });
  for (auto& thread : pool) thread.join();
  return counter;
}
int main() {
  for (bool spin : {false, true}) {
    xe::ps5_spin_mutexes = spin;
    const long global = Hammer<xe::xe_global_mutex, true>(8, 40000);
    const long fast = Hammer<xe::xe_fast_mutex, false>(8, 40000);
    std::printf("%s: global %ld, fast %ld of %d\n", spin ? "spinning" : "blocking", global, fast, 8 * 40000);
    assert(global == 8 * 40000 && fast == 8 * 40000);
    // A lock held by another thread is refused by try_lock and granted after release.
    xe::xe_global_mutex held;
    held.lock();
    bool refused = false;
    std::thread([&] { refused = !held.try_lock(); }).join();
    assert(refused);
    held.unlock();
    bool granted = false;
    std::thread([&] { granted = held.try_lock(); if (granted) held.unlock(); }).join();
    assert(granted);
  }
  std::puts("PASS: recursive and plain mutexes keep exclusion in both modes; try_lock refuses and grants as expected");
}
'''
(out / 'check.cpp').write_text(harness)
for flags in (['-fsanitize=thread'], ['-fsanitize=address,undefined']):
    subprocess.run(['clang++-18', '-std=c++20', '-O1', '-g', '-pthread', *flags, str(out / 'check.cpp'), '-o', str(out / 'check')], check=True)
    subprocess.run([str(out / 'check')], check=True)
