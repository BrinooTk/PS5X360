// SPDX-License-Identifier: MIT
// Exercise the actual Xenia POSIX threading implementation, not mocks.
#include "xenia/base/threading.h"
#include <atomic>
#include <cstdio>
#include <vector>
using namespace std::chrono_literals;
namespace t = xe::threading;
int main() {
  unsigned cases = 0, failures = 0;
  auto check = [&](bool ok, const char* name) {
    ++cases;
    if (!ok) { ++failures; std::printf("FAIL %s\n", name); }
  };
  auto manual = t::Event::CreateManualResetEvent(false);
  auto automatic = t::Event::CreateAutoResetEvent(true);
  check(manual && automatic, "event allocation");
  if (!manual || !automatic) return 1;
  check(t::Wait(manual.get(), false, 1ms) == t::WaitResult::kTimeout, "unsignaled timeout");
  manual->Set();
  check(t::Wait(manual.get(), false, 0ms) == t::WaitResult::kSuccess, "manual set");
  check(t::Wait(manual.get(), false, 0ms) == t::WaitResult::kSuccess, "manual retains state");
  manual->Reset();
  check(t::Wait(manual.get(), false, 0ms) == t::WaitResult::kTimeout, "manual reset");
  check(t::Wait(automatic.get(), false, 0ms) == t::WaitResult::kSuccess, "auto consumes state");
  check(t::Wait(automatic.get(), false, 0ms) == t::WaitResult::kTimeout, "auto reset");
  t::WaitHandle* pair[] = {manual.get(), automatic.get()};
  automatic->Set();
  auto any = t::WaitAny(pair, 2, false, 0ms);
  check(any.first == t::WaitResult::kSuccess && any.second == 1, "wait any returns correct handle");
  manual->Set(); automatic->Set();
  check(t::WaitAll(pair, 2, false, 0ms) == t::WaitResult::kSuccess, "wait all");
  check(t::WaitAll(pair, 2, false, 0ms) == t::WaitResult::kTimeout, "wait all consumes auto event");
  auto sem = t::Semaphore::Create(1, 2);
  check(sem && t::Wait(sem.get(), false, 0ms) == t::WaitResult::kSuccess, "semaphore initial token");
  if (!sem) return 1;
  check(t::Wait(sem.get(), false, 0ms) == t::WaitResult::kTimeout, "semaphore consumes token");
  int previous = -1;
  check(sem->Release(2, &previous) && previous == 0, "semaphore release count");
  check(!sem->Release(1, nullptr), "semaphore capacity bound");
  check(t::Wait(sem.get(), false, 0ms) == t::WaitResult::kSuccess &&
        t::Wait(sem.get(), false, 0ms) == t::WaitResult::kSuccess &&
        t::Wait(sem.get(), false, 0ms) == t::WaitResult::kTimeout, "semaphore exact token count");
  auto gate = t::Event::CreateManualResetEvent(false);
  std::atomic<unsigned> completed{0};
  std::vector<std::unique_ptr<t::Thread>> workers;
  for (unsigned i = 0; i < 4; ++i) {
    auto worker = t::Thread::Create({}, [&] {
      if (t::Wait(gate.get(), false, 2s) == t::WaitResult::kSuccess)
        completed.fetch_add(1);
    });
    check(bool(worker), "real worker creation");
    if (!worker) return 1;
    workers.push_back(std::move(worker));
  }
  gate->Set();
  for (auto& worker : workers)
    check(t::Wait(worker.get(), false, 3s) == t::WaitResult::kSuccess, "real worker join");
  check(completed == 4, "manual event wakes all workers");
  auto timer = t::Timer::CreateSynchronizationTimer();
  check(bool(timer), "timer allocation");
  if (!timer) return 1;
  check(timer->SetOnceAfter(5ms), "timer arm");
  check(t::Wait(timer.get(), false, 2s) == t::WaitResult::kSuccess, "real timer dispatch");
  check(t::Wait(timer.get(), false, 0ms) == t::WaitResult::kTimeout, "synchronization timer consumed");
  check(timer->SetOnceAfter(1s) && timer->Cancel(), "timer cancellation");
  check(t::Wait(timer.get(), false, 10ms) == t::WaitResult::kTimeout, "cancelled timer stays unsignaled");
  auto self = t::Thread::GetCurrentThread();
  unsigned order = 0;
  bool ordered = true;
  self->QueueUserCallback([&] { ordered &= ++order == 1; });
  self->QueueUserCallback([&] { ordered &= ++order == 2; });
  manual->Reset();
  check(t::Wait(manual.get(), false, 0ms) == t::WaitResult::kTimeout && order == 0,
        "APC deferred during non-alertable wait");
  check(t::Wait(manual.get(), true, 0ms) == t::WaitResult::kUserCallback && order == 2 && ordered,
        "APC FIFO drained on target thread");
  self->QueueUserCallback([&] { self->QueueUserCallback([&] { ++order; }); });
  check(t::AlertableSleep(1s) == t::SleepResult::kAlerted && order == 3,
        "APC reentrant enqueue without deadlock");
  auto ready = t::Event::CreateManualResetEvent(false);
  std::atomic<bool> delivered{false};
  std::atomic<bool> callback_result{false};
  auto receiver = t::Thread::Create({}, [&] {
    ready->Set();
    callback_result = t::Wait(manual.get(), true, 2s) == t::WaitResult::kUserCallback;
  });
  check(bool(receiver), "APC receiver allocation");
  if (!receiver) return 1;
  check(t::Wait(ready.get(), false, 2s) == t::WaitResult::kSuccess, "APC receiver ready");
  receiver->QueueUserCallback([&] { delivered = true; });
  check(t::Wait(receiver.get(), false, 3s) == t::WaitResult::kSuccess && delivered && callback_result,
        "cross-thread APC wakes alertable wait");
  std::atomic<bool> ran_suspended{false};
  t::Thread::CreationParameters suspended_params;
  suspended_params.create_suspended = true;
  auto suspended = t::Thread::Create(suspended_params, [&] { ran_suspended = true; });
  check(bool(suspended), "suspended thread creation");
  if (!suspended) return 1;
  check(suspended->system_id() != 0 && !ran_suspended, "suspended thread published ID before execution");
  uint32_t count = 0;
  check(suspended->Resume(&count) && count == 1, "initial suspend count resumes exactly once");
  check(t::Wait(suspended.get(), false, 3s) == t::WaitResult::kSuccess && ran_suspended,
        "resumed thread executes and joins");
  std::printf("ACTUAL THREAD CASES %u FAILURES %u\n", cases, failures);
  return failures ? 1 : 0;
}
