// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <utility>

namespace xbox360ps5::gpu_diag {
enum class Kind : unsigned { pipeline, submit, present, fence, idle, cpu_draw, cpu_copy, cpu_upload, cpu_completion, count };
inline constexpr const char* names[] = {"pipeline", "submit", "present", "fence", "idle", "cpu-draw-sampled-1of1024", "cpu-copy", "cpu-upload-sampled-1of1024", "cpu-completion"};
inline std::atomic<bool> enabled{false};
inline std::atomic<bool> stages_enabled{false};
// How far around a guest write the GPU's copy of memory is invalidated on
// speculation, in bytes (a power of two; 0 keeps the core's own 256 KiB
// block). Read on every write fault, so it can be changed while a game runs
// to compare the same scene. 16 KiB is one host page: in the experimental.7
// session it had the fewest watch closes on the GPU thread (about 2,500 a
// second against 4,200 at 64 KiB and 5,300 at 256 KiB), with more faults.
inline std::atomic<uint32_t> invalidation_window{0x4000};
// Whether a range request whose pages are all valid is answered without the
// global critical region (see RangeBitsAllSet). On by default in experimental
// builds; can be switched while a game runs to compare the same scene.
inline std::atomic<bool> lock_free_valid{true};
// Whether memory that the guest keeps rewriting is left unwatched and compared
// by content when a draw asks for it (xenia/gpu/shared_memory.cc). Can be
// switched while a game runs: off, such pages go back to being watched as they
// are next uploaded.
inline std::atomic<bool> unwatched_pages{true};
// Whether a draw may take its data from guest memory the game has released
// (pages its heap marks as not accessible), when the host can still read them.
inline std::atomic<bool> read_released_pages{false};
// A folder for the bytes of small resolves and small textures (a diagnosis asked
// for by a file in the title's folder), or null. Set before the game starts.
inline const char* dump_folder = nullptr;
// Draws made with the stand-in pixel shader of asynchronous pipeline creation
// (their real pipeline was still being compiled), and draws that waited for
// the real one instead. For the periodic performance line.
inline std::atomic<unsigned long long> stand_in_draws{0}, pipeline_waits{0};
// True when bits first..last of a bitmap of 64-bit words are all set. The words
// are written by other threads under a lock and only read here: a request that
// sees a page valid just before a guest write invalidates it is a draw ordered
// before that write, which the locked check allows as well.
inline bool RangeBitsAllSet(const uint64_t* words, uint32_t first, uint32_t last) {
  const uint32_t block_first = first >> 6, block_last = last >> 6;
  for (uint32_t block = block_first; block <= block_last; ++block) {
    uint64_t need = UINT64_MAX;
    if (block == block_first) need &= ~((uint64_t(1) << (first & 63)) - 1);
    if (block == block_last && (last & 63) != 63) need &= (uint64_t(1) << ((last & 63) + 1)) - 1;
    if ((__atomic_load_n(&words[block], __ATOMIC_ACQUIRE) & need) != need) return false;
  }
  return true;
}
struct Reading { uint64_t calls = 0, nanoseconds = 0, worst = 0, errors = 0; };
struct Counter {
  std::atomic<uint64_t> calls{0}, nanoseconds{0}, worst{0}, errors{0};
  void Add(uint64_t ns, bool failed) {
    calls.fetch_add(1, std::memory_order_relaxed);
    nanoseconds.fetch_add(ns, std::memory_order_relaxed);
    if (failed) errors.fetch_add(1, std::memory_order_relaxed);
    auto old = worst.load(std::memory_order_relaxed);
    while (old < ns && !worst.compare_exchange_weak(old, ns, std::memory_order_relaxed)) {}
  }
  Reading Take() {
    // Counters may straddle one summary boundary; no render-thread lock.
    return {calls.exchange(0, std::memory_order_relaxed),
            nanoseconds.exchange(0, std::memory_order_relaxed),
            worst.exchange(0, std::memory_order_relaxed),
            errors.exchange(0, std::memory_order_relaxed)};
  }
};
inline std::array<Counter, static_cast<unsigned>(Kind::count)> counters;
// Inclusive host wall time: nested scopes overlap and must not be added.
// Draw/upload readings count only timed samples; totals are not full workload totals.
// Disabled mode does not read the clock or update counters.
class Scope {
 public:
  explicit Scope(Kind kind) : kind_(kind), active_(enabled.load(std::memory_order_relaxed) && stages_enabled.load(std::memory_order_relaxed)) {
    // These paths may run over 100,000 times per second. Clock reads and
    // atomic accounting on every invocation would distort the workload.
    if (active_ && (kind == Kind::cpu_draw || kind == Kind::cpu_upload)) {
      static thread_local std::array<uint64_t, static_cast<unsigned>(Kind::count)> seen{};
      active_ = (seen[static_cast<unsigned>(kind)]++ & 1023) == 0;
    }
    if (active_) started_ = std::chrono::steady_clock::now();
  }
  ~Scope() {
    if (!active_) return;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started_).count();
    counters[static_cast<unsigned>(kind_)].Add(uint64_t(ns), false);
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
 private:
  Kind kind_;
  bool active_;
  std::chrono::steady_clock::time_point started_{};
};

template<class Fn, class... Args>
auto Call(Kind kind, Fn fn, Args&&... args) {
  if (!enabled.load(std::memory_order_relaxed)) return fn(std::forward<Args>(args)...);
  const auto started = std::chrono::steady_clock::now();
  const auto result = fn(std::forward<Args>(args)...);
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - started).count();
  counters[static_cast<unsigned>(kind)].Add(uint64_t(ns), int(result) < 0);
  return result;
}
inline void Reset() { for (auto& c : counters) c.Take(); }
// These are API wall times, including waits; they are not GPU utilization.
}
