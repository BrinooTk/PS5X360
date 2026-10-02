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
    target = root / "build/generated-sources/threading_posix.cc"
    if not target.exists() or target.read_text() != data:
        target.write_text(data)
