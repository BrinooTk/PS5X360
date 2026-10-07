// SPDX-License-Identifier: MIT
#include "xbox360ps5/canary_audio.hpp"
#include "xenia/apu/audio_driver.h"
#include "xenia/apu/conversion.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
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
using xe::X_STATUS;
namespace {
constexpr uint32_t kGrain = 256;  // Samples per channel in one port output.

class PacedDriver final : public xe::apu::AudioDriver {
 public:
  PacedDriver(xe::threading::Semaphore* semaphore, uint32_t frequency, uint32_t channels, bool convert)
      : semaphore_(semaphore), frequency_(frequency), channels_(channels), convert_(convert),
        // As Canary's own drivers: 256 samples per channel in a 5.1 frame, 768 in a stereo one.
        channel_samples_(channels == 6 ? 256 : 768) {}
  ~PacedDriver() override { Shutdown(); }

  bool Initialize() override {
#if XE_PLATFORM_PS5
    // The port takes 48 kHz stereo; voices at other rates are consumed in time
    // without sound. The main port belongs to the system user (255), not to
    // the signed-in one. Parameter 1 means 16-bit stereo.
    if (frequency_ == 48000) {
      const int initialized = sceAudioOutInit();
      port_ = sceAudioOutOpen(0xff, 0, 0, kGrain, 48000, 1);
      if (port_ < 0) {
        XELOGW("AudioOut open failed {:08X} (init {:08X}): this voice is silent", unsigned(port_),
               unsigned(initialized));
      } else {
        XELOGW("AudioOut port {} open", port_);
      }
    }
#endif
    worker_ = std::thread([this] { Run(); });
    return true;
  }

  void Shutdown() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) return;
      stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
#if XE_PLATFORM_PS5
    if (port_ >= 0) sceAudioOutClose(port_);
    port_ = -1;
#endif
  }

  void SubmitFrame(float* samples) override {
    std::vector<float> frame(samples, samples + size_t(channels_) * channel_samples_);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      frames_.push_back(std::move(frame));
    }
    wake_.notify_all();
  }

  void Pause() override { SetPaused(true); }
  void Resume() override { SetPaused(false); }
  void SetVolume(float volume) override { volume_ = volume; }

 private:
  void SetPaused(bool paused) {
    { std::lock_guard<std::mutex> lock(mutex_); paused_ = paused; }
    wake_.notify_all();
  }

  // The frame as interleaved stereo, native floats.
  void ToStereo(const std::vector<float>& frame, std::vector<float>& stereo) const {
    stereo.resize(size_t(channel_samples_) * 2);
    if (convert_ && channels_ == 6) {
      xe::apu::conversion::sequential_6_BE_to_interleaved_2_LE(stereo.data(), frame.data(), channel_samples_);
    } else if (channels_ == 2) {
      std::copy(frame.begin(), frame.end(), stereo.begin());
    } else {
      // Interleaved 5.1: front left and right, with the centre in both.
      for (uint32_t n = 0; n < channel_samples_; ++n) {
        const float* in = &frame[size_t(n) * channels_];
        stereo[n * 2] = in[0] + in[2] * 0.7071f;
        stereo[n * 2 + 1] = in[1] + in[2] * 0.7071f;
      }
    }
  }

  void Run() {
    xe::threading::set_name("Xbox360PS5 audio");
    auto next = std::chrono::steady_clock::now();
    std::vector<float> stereo;
    std::array<int16_t, kGrain * 2> grain;
    for (;;) {
      std::vector<float> frame;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return stopping_ || (!paused_ && !frames_.empty()); });
        if (stopping_) return;
        frame = std::move(frames_.front());
        frames_.pop_front();
      }
      ToStereo(frame, stereo);
      const float volume = cvars::mute ? 0.0f : volume_ * audio_volume.load(std::memory_order_relaxed);
      int peak = 0;
      bool played = false;
      for (uint32_t at = 0; at + kGrain <= channel_samples_; at += kGrain) {
        for (uint32_t n = 0; n < kGrain * 2; ++n) {
          const float sample = stereo[size_t(at) * 2 + n] * volume;
          const float clamped = std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
          grain[n] = int16_t(std::lround(clamped * 32767.0f));
          peak = std::max(peak, std::abs(int(grain[n])));
        }
#if XE_PLATFORM_PS5
        // The port blocks until it has taken the grain: that is the clock.
        if (port_ >= 0 && sceAudioOutOutput(port_, grain.data()) >= 0) played = true;
#endif
      }
      if (!played) {
        // No port: keep the real rate by the clock (never more than 50 ms behind).
        next = std::max(next, std::chrono::steady_clock::now() - std::chrono::milliseconds(50)) +
               std::chrono::microseconds(uint64_t(channel_samples_) * 1000000 / frequency_);
        std::unique_lock<std::mutex> lock(mutex_);
        if (wake_.wait_until(lock, next, [this] { return stopping_; })) return;
      }
      Meter(peak);
      semaphore_->Release(1, nullptr);
    }
  }

  // What the game submits: silence here means the game (or its decoding)
  // produces none, not that the output is at fault.
  void Meter(int peak) {
    peak_ = std::max(peak_, peak);
    ++total_;
    if (++metered_ < 1024) return;
    XELOGI("Audio level: peak {} of 32767 in the last {} frames ({} submitted)", peak_, metered_, total_);
    peak_ = 0;
    metered_ = 0;
  }

  xe::threading::Semaphore* semaphore_;
  const uint32_t frequency_, channels_;
  const bool convert_;
  const uint32_t channel_samples_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::vector<float>> frames_;
  std::thread worker_;
  bool stopping_ = false, paused_ = false;
  float volume_ = 1.0f;
  int port_ = -1, peak_ = 0;
  unsigned metered_ = 0, total_ = 0;
};
}

xe::X_STATUS CanaryAudioSystem::CreateDriver(size_t, xe::threading::Semaphore* semaphore,
                                             xe::apu::AudioDriver** out_driver) {
  // A game's client: 5.1 frames at 48 kHz, in the console's byte order.
  auto driver = new PacedDriver(semaphore, xe::apu::AudioDriver::kFrameFrequencyDefault,
                                xe::apu::AudioDriver::kFrameChannelsDefault, true);
  driver->Initialize();
  *out_driver = driver;
  return X_STATUS_SUCCESS;
}

xe::apu::AudioDriver* CanaryAudioSystem::CreateDriver(xe::threading::Semaphore* semaphore, uint32_t frequency,
                                                      uint32_t channels, bool need_format_conversion) {
  // A media voice: its owner initializes it.
  return new PacedDriver(semaphore, frequency, channels, need_format_conversion);
}

void CanaryAudioSystem::DestroyDriver(xe::apu::AudioDriver* driver) {
  driver->Shutdown();
  delete driver;
}
}
