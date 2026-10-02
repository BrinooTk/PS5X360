// SPDX-License-Identifier: MIT
#include "xbox360ps5/dualsense_input.hpp"
#include "xenia/hid/input_system.h"
#include <cstdio>
using xe::X_RESULT;
using xe::X_STATUS;
using namespace xe::hid;
int main() {
  unsigned cases = 0, failed = 0;
  auto check = [&](bool ok, const char* name) { ++cases; if (!ok) { ++failed; std::printf("FAIL %s\n", name); } };
  InputSystem input(nullptr);
  auto owned = std::make_unique<xbox360ps5::DualSenseInput>();
  auto& driver = *owned;
  check(input.Setup() == X_STATUS_SUCCESS && driver.Setup() == X_STATUS_SUCCESS, "actual input initialization");
  input.AddDriver(std::move(owned));
  X_INPUT_STATE state{};
  X_INPUT_CAPABILITIES caps{};
  check(input.GetState(0, &state) == X_ERROR_DEVICE_NOT_CONNECTED, "no invented connection");
  xbox360ps5::PadSample sample;
  sample.connected = true;
  driver.Submit(sample);
  check(input.GetState(0, &state) == X_ERROR_SUCCESS && !uint16_t(state.gamepad.buttons) && !int16_t(state.gamepad.thumb_lx), "centered controller");
  const uint32_t packet = state.packet_number;
  driver.Submit(sample); input.GetState(0, &state);
  check(uint32_t(state.packet_number) == packet, "unchanged packet stable");
  sample.buttons = 0x4000 | 0x10 | 0x8 | 0x400;
  sample.lx = 255; sample.ly = 0; sample.rx = 0; sample.ry = 255;
  sample.l2 = 93; sample.r2 = 247;
  driver.Submit(sample); input.GetState(0, &state);
  check(uint16_t(state.gamepad.buttons) == (X_INPUT_GAMEPAD_A | X_INPUT_GAMEPAD_DPAD_UP | X_INPUT_GAMEPAD_START | X_INPUT_GAMEPAD_LEFT_SHOULDER), "face and directional mapping through actual router");
  check(state.gamepad.thumb_lx == 32767 && state.gamepad.thumb_ly == 32767 && state.gamepad.thumb_rx == -32768 && state.gamepad.thumb_ry == -32767, "axes full range and Y inversion");
  check(state.gamepad.left_trigger == 93 && state.gamepad.right_trigger == 247, "analog trigger precision");
  const auto* bytes = reinterpret_cast<const unsigned char*>(&state.gamepad);
  check(bytes[0] == 0x11 && bytes[1] == 0x11 && bytes[4] == 0x7F && bytes[5] == 0xFF, "guest big endian ABI");
  check(input.GetCapabilities(0, 1, &caps) == X_ERROR_SUCCESS && caps.type == 1 && caps.sub_type == 1 && !uint16_t(caps.flags), "accurate gamepad capabilities");
  check(input.GetState(1, &state) == X_ERROR_DEVICE_NOT_CONNECTED, "second controller absent");
  X_INPUT_KEYSTROKE key{};
  unsigned downs = 0;
  while (input.GetKeystroke(0, 0, &key) == X_ERROR_SUCCESS) downs += key.flags == X_INPUT_KEYSTROKE_KEYDOWN;
  check(downs == 4, "button down events routed");
  sample.buttons = 0; driver.Submit(sample);
  unsigned ups = 0;
  while (input.GetKeystroke(0, 0, &key) == X_ERROR_SUCCESS) ups += key.flags == X_INPUT_KEYSTROKE_KEYUP;
  check(ups == 4, "button release events routed");
  driver.set_is_active_callback([] { return false; });
  input.GetState(0, &state);
  check(!int16_t(state.gamepad.thumb_lx) && state.gamepad.right_trigger == 0, "UI capture neutralizes guest controls");
  sample.connected = false; driver.Submit(sample);
  check(input.GetState(0, &state) == X_ERROR_DEVICE_NOT_CONNECTED, "disconnect propagated");
  std::printf("ACTUAL INPUT ROUTER CASES %u FAILURES %u\n", cases, failed);
  return failed ? 1 : 0;
}
