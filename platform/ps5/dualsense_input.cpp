// SPDX-License-Identifier: MIT
#include "xbox360ps5/dualsense_input.hpp"
#include "xenia/ui/virtual_key.h"
#include <algorithm>
#include <cstring>
using xe::X_STATUS;
using xe::X_RESULT;
using namespace xe::hid;
namespace xbox360ps5 {
namespace {
struct Button { uint32_t native; uint16_t guest; xe::ui::VirtualKey key; };
constexpr Button buttons[] = {
 {0x10, X_INPUT_GAMEPAD_DPAD_UP, xe::ui::VirtualKey::kXInputPadDpadUp},
 {0x40, X_INPUT_GAMEPAD_DPAD_DOWN, xe::ui::VirtualKey::kXInputPadDpadDown},
 {0x80, X_INPUT_GAMEPAD_DPAD_LEFT, xe::ui::VirtualKey::kXInputPadDpadLeft},
 {0x20, X_INPUT_GAMEPAD_DPAD_RIGHT, xe::ui::VirtualKey::kXInputPadDpadRight},
 {0x08, X_INPUT_GAMEPAD_START, xe::ui::VirtualKey::kXInputPadStart},
 {0x100000, X_INPUT_GAMEPAD_BACK, xe::ui::VirtualKey::kXInputPadBack},  // Touchpad click.
 {0x02, X_INPUT_GAMEPAD_LEFT_THUMB, xe::ui::VirtualKey::kXInputPadLThumbPress},
 {0x04, X_INPUT_GAMEPAD_RIGHT_THUMB, xe::ui::VirtualKey::kXInputPadRThumbPress},
 {0x400, X_INPUT_GAMEPAD_LEFT_SHOULDER, xe::ui::VirtualKey::kXInputPadLShoulder},
 {0x800, X_INPUT_GAMEPAD_RIGHT_SHOULDER, xe::ui::VirtualKey::kXInputPadRShoulder},
 {0x4000, X_INPUT_GAMEPAD_A, xe::ui::VirtualKey::kXInputPadA},
 {0x2000, X_INPUT_GAMEPAD_B, xe::ui::VirtualKey::kXInputPadB},
 {0x8000, X_INPUT_GAMEPAD_X, xe::ui::VirtualKey::kXInputPadX},
 {0x1000, X_INPUT_GAMEPAD_Y, xe::ui::VirtualKey::kXInputPadY},
};
int16_t Axis(uint8_t value, bool invert = false) {
  const int delta = int(value) - 128;
  int result = delta < 0 ? delta * 256 : delta * 32767 / 127;
  return int16_t(std::clamp(invert ? -result : result, -32768, 32767));
}
}
DualSenseInput::DualSenseInput(Rumble rumble, uint32_t user) : InputDriver(nullptr, 0), user_(user), rumble_(std::move(rumble)) {}
X_STATUS DualSenseInput::Setup() { return X_STATUS_SUCCESS; }
void DualSenseInput::Submit(const PadSample& sample) {
  std::lock_guard<std::mutex> lock(mutex_);
  X_INPUT_GAMEPAD next{};
  if (sample.connected) {
    uint16_t held = 0;
    for (auto button : buttons) if (sample.buttons & button.native) held |= button.guest;
    next.buttons = held;
    next.left_trigger = sample.l2; next.right_trigger = sample.r2;
    next.thumb_lx = Axis(sample.lx); next.thumb_ly = Axis(sample.ly, true);
    next.thumb_rx = Axis(sample.rx); next.thumb_ry = Axis(sample.ry, true);
  }
  if (connected_ != sample.connected || std::memcmp(&next, &state_.gamepad, sizeof(next)))
    state_.packet_number = uint32_t(state_.packet_number) + 1;
  if (sample.connected) {
    const uint16_t old = connected_ ? uint16_t(state_.gamepad.buttons) : 0;
    const uint16_t held = next.buttons;
    for (auto button : buttons) if ((old ^ held) & button.guest) {
      X_INPUT_KEYSTROKE key{};
      key.user_index = uint8_t(user_);
      key.virtual_key = uint16_t(button.key);
      key.flags = held & button.guest ? X_INPUT_KEYSTROKE_KEYDOWN : X_INPUT_KEYSTROKE_KEYUP;
      if (keys_.size() == 256) keys_.pop_front();
      keys_.push_back(key);
    }
  } else keys_.clear();
  connected_ = sample.connected;
  state_.gamepad = next;
}
X_RESULT DualSenseInput::GetState(uint32_t user, X_INPUT_STATE* out) {
  if (!out) return X_ERROR_BAD_ARGUMENTS;
  std::lock_guard<std::mutex> lock(mutex_);
  if (user != user_ || !connected_) return X_ERROR_DEVICE_NOT_CONNECTED;
  *out = state_;
#if !XBOX360PS5_CANARY  // Canary has no window focus gate for input drivers.
  if (!is_active()) out->gamepad = {};
#endif
  return X_ERROR_SUCCESS;
}
X_RESULT DualSenseInput::GetCapabilities(uint32_t user, uint32_t flags, X_INPUT_CAPABILITIES* out) {
  if (!out || (flags & ~uint32_t(X_INPUT_FLAG_GAMEPAD))) return X_ERROR_BAD_ARGUMENTS;
  std::lock_guard<std::mutex> lock(mutex_);
  if (user != user_ || !connected_) return X_ERROR_DEVICE_NOT_CONNECTED;
  *out = {};
  out->type = 1; out->sub_type = 1;
  if (rumble_) { out->flags = X_INPUT_CAPS_FFB_SUPPORTED; out->vibration.left_motor_speed = 65535; out->vibration.right_motor_speed = 65535; }
  out->gamepad.buttons = 0xF3FF;
  out->gamepad.left_trigger = 255; out->gamepad.right_trigger = 255;
  out->gamepad.thumb_lx = out->gamepad.thumb_ly = out->gamepad.thumb_rx = out->gamepad.thumb_ry = 32767;
  return X_ERROR_SUCCESS;
}
X_RESULT DualSenseInput::SetState(uint32_t user, X_INPUT_VIBRATION* vibration) {
  if (!vibration) return X_ERROR_BAD_ARGUMENTS;
  { std::lock_guard<std::mutex> lock(mutex_);
    if (user != user_ || !connected_) return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (!rumble_) return X_RESULT_FROM_WIN32(50);  // Motor output not connected yet.
  return rumble_(vibration->left_motor_speed, vibration->right_motor_speed)
      ? X_ERROR_SUCCESS : X_ERROR_FUNCTION_FAILED;
}
X_RESULT DualSenseInput::GetKeystroke(uint32_t user, uint32_t, X_INPUT_KEYSTROKE* out) {
  if (!out) return X_ERROR_BAD_ARGUMENTS;
  std::lock_guard<std::mutex> lock(mutex_);
  if ((user != user_ && user != 255) || !connected_) return X_ERROR_DEVICE_NOT_CONNECTED;
#if XBOX360PS5_CANARY
  if (keys_.empty()) return X_ERROR_EMPTY;
#else
  if (!is_active() || keys_.empty()) return X_ERROR_EMPTY;
#endif
  *out = keys_.front(); keys_.pop_front();
  return X_ERROR_SUCCESS;
}
}
