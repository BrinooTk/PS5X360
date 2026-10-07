// SPDX-License-Identifier: MIT
// The audio system for the Xenia Canary core. Every client (a game may register
// several) and every media voice gets a driver with a thread of its own, which
// takes the queued frames at the real rate and frees the game's audio thread.
// On the console the frames go to an AudioOut port; when no port can be
// opened, and on the PC, they are only consumed in time: a game whose audio
// client fails to register may give up on its sound engine and then stall.
#pragma once
#include "xenia/apu/audio_system.h"
#include <atomic>
namespace xbox360ps5 {
// The volume chosen in the settings, over every game voice (1 is unchanged).
inline std::atomic<float> audio_volume{1.0f};
class CanaryAudioSystem final : public xe::apu::AudioSystem {
 public:
  explicit CanaryAudioSystem(xe::cpu::Processor* processor) : AudioSystem(processor) {}
  std::string name() const override { return "Xbox360PS5"; }
  xe::X_STATUS CreateDriver(size_t index, xe::threading::Semaphore* semaphore,
                            xe::apu::AudioDriver** out_driver) override;
  xe::apu::AudioDriver* CreateDriver(xe::threading::Semaphore* semaphore, uint32_t frequency,
                                     uint32_t channels, bool need_format_conversion) override;
  void DestroyDriver(xe::apu::AudioDriver* driver) override;
};
}
