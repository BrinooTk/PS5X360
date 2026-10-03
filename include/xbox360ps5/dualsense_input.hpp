// SPDX-License-Identifier: MIT
#pragma once
#include "xenia/hid/input_driver.h"
#include <deque>
#include <mutex>
namespace xbox360ps5 {
struct PadSample {
  uint32_t buttons = 0;
  uint8_t lx = 128, ly = 128, rx = 128, ry = 128, l2 = 0, r2 = 0;
  bool connected = false;
};
// Submit the already acquired native pad sample; guest queries never call UI APIs.
class DualSenseInput final : public xe::hid::InputDriver {
 public:
  using Rumble = std::function<bool(uint16_t, uint16_t)>;
  explicit DualSenseInput(Rumble rumble = {});
  void Submit(const PadSample& sample);
  xe::X_STATUS Setup() override;
  xe::X_RESULT GetCapabilities(uint32_t, uint32_t, xe::hid::X_INPUT_CAPABILITIES*) override;
  xe::X_RESULT GetState(uint32_t, xe::hid::X_INPUT_STATE*) override;
  xe::X_RESULT SetState(uint32_t, xe::hid::X_INPUT_VIBRATION*) override;
  xe::X_RESULT GetKeystroke(uint32_t, uint32_t, xe::hid::X_INPUT_KEYSTROKE*) override;
#if XBOX360PS5_CANARY
  xe::hid::InputType GetInputType() const override { return xe::hid::InputType::Controller; }
#endif
 private:
  std::mutex mutex_;
  bool connected_ = false;
  xe::hid::X_INPUT_STATE state_{};
  std::deque<xe::hid::X_INPUT_KEYSTROKE> keys_;
  Rumble rumble_;
};
}
