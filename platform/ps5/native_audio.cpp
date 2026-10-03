// SPDX-License-Identifier: MIT
#include "xbox360ps5/native_audio.hpp"
#include "xenia/apu/audio_driver.h"
#include "xenia/apu/conversion.h"
#include "xenia/apu/apu_flags.h"
#include "xenia/base/logging.h"
#include "xenia/base/threading.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <deque>
#include <thread>
using xe::X_STATUS;
#if XE_PLATFORM_PS5
extern "C" {
int sceAudioOutInit();
int sceAudioOutOpen(int32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t);
int sceAudioOutOutput(int32_t, const void*);
int sceAudioOutClose(int32_t);
}
#endif
DECLARE_bool(mute);
namespace xbox360ps5 {
void ConvertXboxAudioFrame(const float* input, int16_t* stereo) {
  std::array<float, 512> mixed;
  xe::apu::conversion::sequential_6_BE_to_interleaved_2_LE(mixed.data(), input, 256);
  for (size_t i = 0; i < mixed.size(); ++i) {
    const float sample = std::isfinite(mixed[i]) ? std::clamp(mixed[i], -1.0f, 1.0f) : 0;
    stereo[i] = sample <= -1 ? -32768 : int16_t(std::lround(sample * 32767));
  }
}
namespace {
// Reports what the game actually submits: silence here means the game (or its
// XMA decoding) produces none, not that the output port is at fault.
void MeterFrame(const int16_t* stereo) {
  static std::mutex mutex;
  static int peak = 0;
  static unsigned frames = 0, total = 0;
  std::lock_guard<std::mutex> lock(mutex);
  for (int i = 0; i < 512; ++i) peak = std::max(peak, std::abs(int(stereo[i])));
  ++total;
  if (++frames < 1024) return;
  XELOGI("Audio level: peak {} of 32767 in the last {} frames ({} submitted)", peak, frames, total);
  peak = 0; frames = 0;
}
// Takes the game's frames at the real rate and plays nothing. A game whose
// audio client cannot register may give up on its whole sound engine and use
// it later all the same, so a missing output port must not fail registration.
class SilentDriver final : public xe::apu::AudioDriver {
 public:
  SilentDriver(xe::Memory* memory, xe::threading::Semaphore* semaphore)
      : AudioDriver(memory), semaphore_(semaphore), worker_([this] { Run(); }) {}
  ~SilentDriver() override {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    wake_.notify_all();
    worker_.join();
  }
  void SubmitFrame(uint32_t samples) override {
    std::array<int16_t, 512> frame;
    ConvertXboxAudioFrame(memory_->TranslateVirtual<float*>(samples), frame.data());
    MeterFrame(frame.data());
    { std::lock_guard<std::mutex> lock(mutex_); ++pending_; }
    wake_.notify_all();
  }
 private:
  void Run() {
    auto next = std::chrono::steady_clock::now();
    for (;;) {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || pending_; });
      if (stopping_) return;
      --pending_;
      // One frame is 256 samples at 48 kHz.
      next = std::max(next, std::chrono::steady_clock::now() - std::chrono::milliseconds(50)) +
             std::chrono::microseconds(5333);
      if (wake_.wait_until(lock, next, [this] { return stopping_; })) return;
      lock.unlock();
      semaphore_->Release(1, nullptr);
    }
  }
  xe::threading::Semaphore* semaphore_;
  std::mutex mutex_;
  std::condition_variable wake_;
  unsigned pending_ = 0;
  bool stopping_ = false;
  std::thread worker_;
};
#if XE_PLATFORM_PS5
class AudioPortDriver final : public xe::apu::AudioDriver {
 public:
  AudioPortDriver(xe::Memory* memory, xe::threading::Semaphore* semaphore)
      : AudioDriver(memory), semaphore_(semaphore) {}
  bool Open() {
    const int initialized = sceAudioOutInit();
    // The main port belongs to the system user (255), not to the signed-in
    // one. Parameter 1 means S16 stereo.
    for (uint32_t grain : {256u, 1024u}) {
      handle_ = sceAudioOutOpen(0xff, 0, 0, grain, 48000, 1);
      if (handle_ >= 0) { batch_ = grain / 256; break; }
    }
    if (handle_ < 0) {
      XELOGE("AudioOut open failed {:08X} (init {:08X})", unsigned(handle_), unsigned(initialized));
      return false;
    }
    XELOGW("AudioOut port {} open, {} frames per output", handle_, batch_ * 256);
    try { worker_ = std::thread([this] { Run(); }); }
    catch (...) { sceAudioOutClose(handle_); handle_ = -1; return false; }
    return true;
  }
  ~AudioPortDriver() override {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    if (handle_ >= 0) sceAudioOutClose(handle_);
  }
  void SubmitFrame(uint32_t samples) override {
    std::array<int16_t, 512> frame;
    ConvertXboxAudioFrame(memory_->TranslateVirtual<float*>(samples), frame.data());
    MeterFrame(frame.data());
    if (cvars::mute) frame.fill(0);
    std::unique_lock<std::mutex> lock(mutex_);
    wake_.wait(lock, [this] { return stopping_ || frames_.size() < 64; });
    if (stopping_) return;
    frames_.push_back(std::move(frame));
    wake_.notify_all();
  }
 private:
  void Run() {
    std::array<int16_t, 2048> grain;
    for (;;) {
      { std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return stopping_ || frames_.size() >= batch_; });
        if (stopping_) return;
        for (unsigned i = 0; i < batch_; ++i) {
          std::copy(frames_.front().begin(), frames_.front().end(), grain.begin() + i * 512);
          frames_.pop_front();
        }
      }
      wake_.notify_all();
      const int result = sceAudioOutOutput(handle_, grain.data());
      if (result < 0 || !semaphore_->Release(int(batch_), nullptr)) {
        std::fprintf(stderr, "AudioOut output/semaphore failure %08x\n", unsigned(result));
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
        wake_.notify_all();
        return;
      }
    }
  }
  xe::threading::Semaphore* semaphore_;
  int handle_ = -1;
  unsigned batch_ = 0;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::array<int16_t, 512>> frames_;
  std::thread worker_;
  bool stopping_ = false;
};
#endif
}
X_STATUS NativeAudioSystem::CreateDriver(size_t, xe::threading::Semaphore* semaphore, xe::apu::AudioDriver** out) {
  if (!out) return X_STATUS_INVALID_PARAMETER;
  *out = nullptr;
  if (!semaphore) return X_STATUS_INVALID_PARAMETER;
#if XE_PLATFORM_PS5
  auto driver = std::make_unique<AudioPortDriver>(memory_, semaphore);
  if (driver->Open()) {
    *out = driver.release();
    return X_STATUS_SUCCESS;
  }
#endif
  *out = new SilentDriver(memory_, semaphore);
  return X_STATUS_SUCCESS;
}
void NativeAudioSystem::DestroyDriver(xe::apu::AudioDriver* driver) { delete driver; }
void NativeAudioSystem::Shutdown() {
  // Stop callbacks before releasing their output queues and native ports.
  // UnregisterClient clears the entries, making repeated shutdown harmless.
  Resume();
  AudioSystem::Shutdown();
  for (size_t i = 0; i < kMaximumClientCount; ++i) {
    if (clients_[i].in_use) UnregisterClient(i);
  }
}
}
