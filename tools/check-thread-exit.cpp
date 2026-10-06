// SPDX-License-Identifier: MIT
// Reproduce the guest self-reference being the last owner at ExTerminateThread.
#include "xenia/base/threading.h"
#include <cassert>
#include <iostream>
int main(int argc, char**) {
  using namespace xe::threading;
  auto ready = Event::CreateManualResetEvent(false);
  auto done = Event::CreateManualResetEvent(false);
  std::unique_ptr<Thread> worker;
  worker = Thread::Create({}, [&] {
    Wait(ready.get(), false);
    if (argc > 1) {
      // The original XThread::Exit ordering: ReleaseHandle then Thread::Exit.
      worker.reset();
      Thread::Exit(0);
    } else {
#ifdef CHECK_FIXED_EXIT
      Thread::ExitWithCleanup(0, [&] { worker.reset(); done->Set(); });
#else
      worker.reset();
      Thread::Exit(0);
#endif
    }
  });
  assert(worker); ready->Set();
  assert(Wait(done.get(), false, std::chrono::seconds(5)) == WaitResult::kSuccess);
  std::cout << "Native exit cleanup completed after final-owner release\n";
}
