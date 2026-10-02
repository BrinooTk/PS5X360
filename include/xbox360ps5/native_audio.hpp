// SPDX-License-Identifier: MIT
#pragma once
#include "xenia/apu/audio_system.h"
namespace xbox360ps5 {
// Xbox output: 256 frames, 6 planar big-endian float channels at 48 kHz.
void ConvertXboxAudioFrame(const float* input, int16_t* stereo);
class NativeAudioSystem final : public xe::apu::AudioSystem {
 public:
  NativeAudioSystem(xe::cpu::Processor* processor, int32_t user)
      : AudioSystem(processor), user_(user) {}
  ~NativeAudioSystem() override { Shutdown(); }
  void Shutdown() override;
 protected:
  xe::X_STATUS CreateDriver(size_t, xe::threading::Semaphore*, xe::apu::AudioDriver**) override;
  void DestroyDriver(xe::apu::AudioDriver*) override;
 private:
  int32_t user_;
};
}
