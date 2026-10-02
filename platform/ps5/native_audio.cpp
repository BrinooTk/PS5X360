// SPDX-License-Identifier: MIT
#include "xbox360ps5/native_audio.hpp"
#include "xenia/apu/audio_driver.h"
#include "xenia/apu/conversion.h"
#include "xenia/apu/apu_flags.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdio>
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
#if XE_PLATFORM_PS5
class AudioPortDriver final : public xe::apu::AudioDriver {
 public:
  AudioPortDriver(xe::Memory* memory, xe::threading::Semaphore* semaphore)
      : AudioDriver(memory), semaphore_(semaphore) {}
  bool Open(int32_t user) {
    (void)sceAudioOutInit();
    // The SDK's parameter 1 means S16 stereo, 0 means mono.
    for (uint32_t grain : {256u, 1024u}) {
      handle_ = sceAudioOutOpen(user, 0, 0, grain, 48000, 1);
      if (handle_ >= 0) { batch_ = grain / 256; break; }
    }
    if (handle_ < 0) { std::fprintf(stderr, "AudioOut open failed %08x\n", unsigned(handle_)); return false; }
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
#if XE_PLATFORM_PS5
  auto driver = std::make_unique<AudioPortDriver>(memory_, semaphore);
  if (!semaphore || !driver->Open(user_)) return X_STATUS_UNSUCCESSFUL;
  *out = driver.release();
  return X_STATUS_SUCCESS;
#else
  return X_STATUS_NOT_IMPLEMENTED;  // Host tests never claim a native audio port.
#endif
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
