"""Generate the pinned real POSIX scheduler overlay without editing Xenia."""


def generate_threads(source, root):
    data = (source / "src/xenia/base/threading_posix.cc").read_text()

    def replace(old, new):
        nonlocal data
        if data.count(old) != 1:
            raise RuntimeError(f"Thread overlay anchor changed: {old[:100]}")
        data = data.replace(old, new)

    replace("#include <sys/eventfd.h>\n#include <sys/syscall.h>",
            "#if XE_PLATFORM_PS5\n#include <pthread_np.h>\n#include <sys/cpuset.h>\nusing cpu_set_t = cpuset_t;\n#else\n#include <sys/syscall.h>\n#endif")
    replace("return static_cast<uint32_t>(syscall(SYS_gettid));",
            "#if XE_PLATFORM_PS5\n  return static_cast<uint32_t>(pthread_getthreadid_np());\n#else\n  return static_cast<uint32_t>(syscall(SYS_gettid));\n#endif")
    replace("if (sigaction(GetSystemSignal(type), &action, nullptr) == -1)",
            "if (sigaction(GetSystemSignal(type), &action, nullptr) == 0)")
    replace("  uint32_t system_id() const { return static_cast<uint32_t>(thread_); }",
            "  uint32_t system_id() const { return system_id_; }")
    replace("  pthread_t thread_;", "  pthread_t thread_;\n  uint32_t system_id_ = 0;")
    replace("  current_thread_ = thread;", "  current_thread_ = thread;\n  thread->handle_.system_id_ = current_thread_system_id();")
    replace("  current_thread_ = new PosixThread(handle);",
            "  current_thread_ = new PosixThread(handle);\n  current_thread_->condition().SetSystemId(current_thread_system_id());")
    replace("  uint32_t system_id() const { return system_id_; }",
            "  uint32_t system_id() const { WaitStarted(); return system_id_; }\n  void SetSystemId(uint32_t id) { system_id_ = id; }")
    replace("      if (pthread_getname_np(thread_, result.data(), result.size() - 1) != 0) {\n        assert_always();\n      }",
            "#if XE_PLATFORM_PS5\n      pthread_get_name_np(thread_, result.data(), result.size() - 1);\n#else\n      if (pthread_getname_np(thread_, result.data(), result.size() - 1) != 0) {\n        assert_always();\n      }\n#endif")
    # BSD names use void-return APIs; affinity uses the SDK's real cpuset_t.
    data = data.replace("pthread_setname_np(thread_, std::string(name).c_str());",
                        "#if XE_PLATFORM_PS5\n      pthread_set_name_np(thread_, name.c_str());\n#else\n      pthread_setname_np(thread_, name.c_str());\n#endif")
    replace("  pthread_setname_np(pthread_self(), std::string(name).c_str());",
            "#if XE_PLATFORM_PS5\n  pthread_set_name_np(pthread_self(), std::string(name).c_str());\n#else\n  pthread_setname_np(pthread_self(), std::string(name).c_str());\n#endif")
    data = data.replace("result |= set << i;", "result |= uint64_t(set) << i;")
    data = data.replace("mask & (1 << i)", "mask & (uint64_t(1) << i)")
    # Linux's uintptr-sized pthread_t is a pointer on BSD. Compare via pthread_equal.
    data = data.replace("pthread_self() == thread_", "pthread_equal(pthread_self(), thread_) != 0")
    # The upstream termination condition is #ifdef, even when the macro is 0.
    data = data.replace("#ifdef XE_PLATFORM_ANDROID", "#if XE_PLATFORM_ANDROID")
    replace("    thread->handle_.state_ =\n        create_suspended ? State::kSuspended : State::kRunning;",
            "    thread->handle_.suspend_count_ = create_suspended ? 1 : 0;\n    thread->handle_.state_ =\n        create_suspended ? State::kSuspended : State::kRunning;")
    replace("    thread->handle_.suspend_count_ = 1;\n    thread->handle_.state_signal_.wait(",
            "    thread->handle_.state_signal_.wait(")
    # APCs must survive a non-alertable wait and run outside signal handlers.
    # A per-thread FIFO plus the existing wait CV avoids calling mutex/std::function
    # from an async signal handler, and removes the unavailable pthread_sigqueue.
    replace("#include <array>", "#include <array>\n#include <deque>")
    replace("thread_local bool alertable_state_ = false;",
            "thread_local bool alertable_state_ = false;\nstatic bool HasCurrentCallbacks();\nstatic void DispatchCurrentCallbacks();")
    begin = data.index("  alertable_state_ = true;", data.index("SleepResult AlertableSleep"))
    end = data.index("\n}", begin)
    data = data[:begin] + '''  auto event = Event::CreateManualResetEvent(false);
  const auto timeout = std::chrono::ceil<std::chrono::milliseconds>(duration);
  return Wait(event.get(), true, timeout) == WaitResult::kUserCallback
      ? SleepResult::kAlerted : SleepResult::kSuccess;''' + data[end:]
    replace("  virtual bool Signal() = 0;", '''  virtual bool Signal() = 0;
  static void NotifyCallback() {
    // Pair with the predicate lock to avoid lost notifications.
    std::lock_guard<std::mutex> lock(mutex_);
    cond_.notify_all();
  }''')
    replace("    auto predicate = [this] { return this->signaled(); };",
            "    auto predicate = [this] { return (alertable_state_ && HasCurrentCallbacks()) || this->signaled(); };")
    replace("    if (executed) {\n      post_execution();", '''    if (executed && alertable_state_ && HasCurrentCallbacks()) {
      lock.unlock();
      DispatchCurrentCallbacks();
      return WaitResult::kUserCallback;
    }
    if (executed) {
      post_execution();''')
    replace("        return operation(handles.cbegin(), handles.cend(), predicate_inner);",
            "        return (alertable_state_ && HasCurrentCallbacks()) || operation(handles.cbegin(), handles.cend(), predicate_inner);")
    replace("    if (wait_success) {", '''    if (wait_success && alertable_state_ && HasCurrentCallbacks()) {
      lock.unlock();
      DispatchCurrentCallbacks();
      return {WaitResult::kUserCallback, 0};
    }
    if (wait_success) {''')
    begin = data.index("    WaitStarted();", data.index("  void QueueUserCallback("))
    end = data.index("\n  bool Resume(", begin)
    data = data[:begin] + '''    WaitStarted();
    if (!callback) return;
    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      user_callbacks_.push_back(std::move(callback));
    }
    NotifyCallback();
  }

  bool HasCallbacks() const {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    return !user_callbacks_.empty();
  }
  void CallUserCallback() {
    for (;;) {
      std::function<void()> callback;
      {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (user_callbacks_.empty()) break;
        callback = std::move(user_callbacks_.front());
        user_callbacks_.pop_front();
      }
      callback();
    }
  }
''' + data[end:]
    replace("  std::function<void()> user_callback_;", "  std::deque<std::function<void()>> user_callbacks_;")
    replace("thread_local PosixThread* current_thread_ = nullptr;", '''thread_local PosixThread* current_thread_ = nullptr;
static bool HasCurrentCallbacks() {
  return current_thread_ && current_thread_->condition().HasCallbacks();
}
static void DispatchCurrentCallbacks() {
  if (current_thread_) current_thread_->condition().CallUserCallback();
}''')
    replace("  install_signal_handler(SignalType::kThreadUserCallback);", "  // APC delivery uses the normal wait queue, not an async signal.")
    begin = data.index("    case SignalType::kThreadUserCallback:", data.index("static void signal_handler(int signal, siginfo_t* info, void* /*context*/)"))
    end = data.index("#if XE_PLATFORM_ANDROID", begin)
    data = data[:begin] + data[end:]
    # Host stacks: a 16 MiB stack per guest thread comes out of the console's
    # 448 MiB flexible-memory budget and the game runs out of threads. With no
    # size asked for, the platform layer gives each thread 2 MiB of direct memory.
    stack = "    if (pthread_attr_setstacksize(&attr, params.stack_size) != 0) {\n      pthread_attr_destroy(&attr);\n      return false;\n    }"
    assert data.count(stack) == 1
    data = data.replace(stack, "#if !XE_PLATFORM_PS5\n" + stack + "\n#endif")
    # Suspend and resume as the console does them. Upstream changed the count
    # outside its lock and let it go below zero when a thread was resumed twice
    # before it woke: the thread then slept for ever. Middleware that suspends
    # and resumes a worker every frame (CRI's, for one) stopped at that point.
    old_resume = """  bool Resume(uint32_t* out_previous_suspend_count = nullptr) {
    if (out_previous_suspend_count) {
      *out_previous_suspend_count = 0;
    }
    WaitStarted();
    std::unique_lock<std::mutex> lock(state_mutex_);
    if (state_ != State::kSuspended) return false;
    if (out_previous_suspend_count) {
      *out_previous_suspend_count = suspend_count_;
    }
    --suspend_count_;
    state_signal_.notify_all();
    return true;
  }
"""
    new_resume = """  bool Resume(uint32_t* out_previous_suspend_count = nullptr) {
    WaitStarted();
    std::unique_lock<std::mutex> lock(state_mutex_);
    if (out_previous_suspend_count) {
      *out_previous_suspend_count = suspend_count_;
    }
    // Resuming a thread that is not suspended does nothing and succeeds.
    if (suspend_count_ == 0) return true;
    if (--suspend_count_ == 0) state_signal_.notify_all();
    return true;
  }
"""
    old_suspend = """    WaitStarted();
    {
      if (out_previous_suspend_count) {
        *out_previous_suspend_count = suspend_count_;
      }
      state_ = State::kSuspended;
      ++suspend_count_;
    }
    int result =
        pthread_kill(thread_, GetSystemSignal(SignalType::kThreadSuspend));
    return result == 0;
  }
"""
    new_suspend = """    WaitStarted();
    const bool self = pthread_equal(pthread_self(), thread_) != 0;
    {
      std::unique_lock<std::mutex> lock(state_mutex_);
      if (out_previous_suspend_count) {
        *out_previous_suspend_count = suspend_count_;
      }
      state_ = State::kSuspended;
      ++suspend_count_;
    }
    // A thread that suspends itself waits right here. Another thread is
    // stopped by a signal, whose handler waits without a condition variable.
    if (self) {
      WaitSuspended();
      return true;
    }
    int result =
        pthread_kill(thread_, GetSystemSignal(SignalType::kThreadSuspend));
    return result == 0;
  }

  /// The wait of a thread another thread suspended, from its signal handler:
  /// the thread may have been inside a condition wait of its own, so this
  /// polls instead of waiting on one.
  void WaitSuspendedInSignal() {
    while (suspend_count_ != 0) usleep(500);
    state_ = State::kRunning;
  }
"""
    old_handler = """      assert_not_null(current_thread_);
      current_thread_->WaitSuspended();"""
    new_handler = """      assert_not_null(current_thread_);
      current_thread_->WaitSuspendedInSignal();"""
    for old, new in ((old_resume, new_resume), (old_suspend, new_suspend), (old_handler, new_handler)):
        assert data.count(old) == 1, old
        data = data.replace(old, new)
    forward = "  void WaitSuspended() { handle_.WaitSuspended(); }"
    assert data.count(forward) == 1
    data = data.replace(forward, forward + "\n  void WaitSuspendedInSignal() { handle_.WaitSuspendedInSignal(); }")
    # An auto-reset event set while threads wait on it belongs to one of those
    # threads, as on the console. Upstream left the signal for whoever checked
    # first, so a thread that set the event and then waited on it itself took
    # its own signal back and the thread it meant to wake slept for ever (a
    # mutex-and-event hand-over between two threads stops exactly there).
    # Each blocking wait takes a ticket; a signal set with waiters present is
    # reserved for the tickets that existed at that moment.
    pairs = [
        ("""    if (predicate()) {
      executed = true;
    } else {
      if (timeout == std::chrono::milliseconds::max()) {
        cond_.wait(lock, predicate);
        executed = true;  // Did not time out;
      } else {
        executed = cond_.wait_for(lock, timeout, predicate);
      }
    }
""", """    TicketScope ticket_scope;
    if (predicate()) {
      executed = true;
    } else {
      wait_ticket_ = ++ticket_counter_;
      begin_wait();
      if (timeout == std::chrono::milliseconds::max()) {
        cond_.wait(lock, predicate);
        executed = true;  // Did not time out;
      } else {
        executed = cond_.wait_for(lock, timeout, predicate);
      }
      end_wait();
    }
"""),
        ("""    std::unique_lock<std::mutex> lock(PosixConditionBase::mutex_);

    bool wait_success = true;
""", """    std::unique_lock<std::mutex> lock(PosixConditionBase::mutex_);

    TicketScope ticket_scope;
    const bool must_block = !predicate();
    if (must_block) {
      wait_ticket_ = ++ticket_counter_;
      for (auto handle : handles) handle->begin_wait();
    }
    bool wait_success = true;
"""),
        ("""    if (wait_success && alertable_state_ && HasCurrentCallbacks()) {
      lock.unlock();
      DispatchCurrentCallbacks();
      return {WaitResult::kUserCallback, 0};
    }
""", """    if (must_block) {
      for (auto handle : handles) handle->end_wait();
    }
    if (wait_success && alertable_state_ && HasCurrentCallbacks()) {
      lock.unlock();
      DispatchCurrentCallbacks();
      return {WaitResult::kUserCallback, 0};
    }
"""),
        ("""  inline virtual bool signaled() const = 0;
  inline virtual void post_execution() = 0;
  static std::condition_variable cond_;
  static std::mutex mutex_;
};

std::condition_variable PosixConditionBase::cond_;
std::mutex PosixConditionBase::mutex_;
""", """  inline virtual bool signaled() const = 0;
  inline virtual void post_execution() = 0;
  // Around the blocking part of a wait, with mutex_ held.
  virtual void begin_wait() {}
  virtual void end_wait() {}
  // The ticket of the wait this thread is blocked in, or zero.
  struct TicketScope {
    ~TicketScope() { wait_ticket_ = 0; }
  };
  static thread_local uint64_t wait_ticket_;
  static uint64_t ticket_counter_;
  static std::condition_variable cond_;
  static std::mutex mutex_;
};

std::condition_variable PosixConditionBase::cond_;
std::mutex PosixConditionBase::mutex_;
thread_local uint64_t PosixConditionBase::wait_ticket_ = 0;
uint64_t PosixConditionBase::ticket_counter_ = 0;
"""),
        ("""  bool Signal() override {
    auto lock = std::unique_lock<std::mutex>(mutex_);
    signal_ = true;
    cond_.notify_all();
    return true;
  }

  void Reset() {
    auto lock = std::unique_lock<std::mutex>(mutex_);
    signal_ = false;
  }

 private:
  inline bool signaled() const override { return signal_; }
  inline void post_execution() override {
    if (!manual_reset_) {
      signal_ = false;
    }
  }
  bool signal_;
""", """  bool Signal() override {
    auto lock = std::unique_lock<std::mutex>(mutex_);
    signal_ = true;
    // With threads already waiting, the signal goes to one of them.
    reserved_ = !manual_reset_ && waiters_ > 0;
    reserved_ticket_ = ticket_counter_;
    cond_.notify_all();
    return true;
  }

  void Reset() {
    auto lock = std::unique_lock<std::mutex>(mutex_);
    signal_ = false;
    reserved_ = false;
  }

 private:
  inline bool signaled() const override {
    return signal_ && (!reserved_ || (wait_ticket_ != 0 && wait_ticket_ <= reserved_ticket_));
  }
  inline void post_execution() override {
    if (!manual_reset_) {
      signal_ = false;
      reserved_ = false;
    }
  }
  void begin_wait() override { ++waiters_; }
  void end_wait() override {
    // Nobody left of those the signal was reserved for: anyone may take it.
    if (--waiters_ == 0) reserved_ = false;
  }
  uint32_t waiters_ = 0;
  bool reserved_ = false;
  uint64_t reserved_ticket_ = 0;
  bool signal_;
"""),
    ]
    for old, new in pairs:
        assert data.count(old) == 1, old
        data = data.replace(old, new)
    target = root / "build/generated-sources/threading_posix.cc"
    if not target.exists() or target.read_text() != data:
        target.write_text(data)
