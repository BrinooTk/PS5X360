// SPDX-License-Identifier: MIT
// Research input store. Does not advertise an Xbox Kinect device.
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
namespace xbox360ps5::motion {
struct Joint { float x = 0, y = 0, z = 0, confidence = 0; };
struct Frame {
  std::array<Joint, 20> joints{};
  uint32_t sequence = 0, session = 0;
  bool tracked = false;
  std::chrono::steady_clock::time_point received{};
};
inline std::atomic<bool> enabled{false};
inline std::atomic<bool> guest_camera_open{false};
inline std::atomic<uint64_t> guest_depth_frames{0}, guest_color_frames{0};
inline std::atomic<uint64_t> guest_completed_requests{0};
inline std::atomic<uint32_t> guest_last_request{0};
// The title's skeleton frame function answers from the estimated pose: frames
// it was given, and how many of them held a body.
inline std::atomic<bool> guest_skeleton_bound{false};
inline std::atomic<uint64_t> guest_skeleton_frames{0}, guest_skeleton_bodies{0};
inline std::mutex mutex;
inline Frame latest;
inline uint64_t packets = 0;
inline void SetEnabled(bool value) {
  if (enabled.load() == value) return;
  std::lock_guard lock(mutex);
  latest = {}; packets = 0; enabled = value;
  if (!value) guest_camera_open=false;
}
enum class Result { accepted, disabled, malformed, replay };
inline uint32_t U32(const char* p) {
  const auto* b = reinterpret_cast<const unsigned char*>(p);
  return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}
inline Result Receive(std::string_view bytes) {
  if (!enabled.load()) return Result::disabled;
  if (bytes.size() != 336 || bytes.substr(0, 4) != "XMP1") return Result::malformed;
  Frame frame;
  frame.sequence = U32(bytes.data() + 4); frame.session = U32(bytes.data() + 8);
  const auto flags = U32(bytes.data() + 12);
  if (!frame.sequence || !frame.session || flags > 1) return Result::malformed;
  frame.tracked = flags == 1;
  for (size_t n = 0; n < 20; ++n) {
    float values[4];
    for (size_t c = 0; c < 4; ++c) {
      const auto bits = U32(bytes.data() + 16 + n * 16 + c * 4);
      std::memcpy(&values[c], &bits, 4);
      if (!std::isfinite(values[c])) return Result::malformed;
    }
    if (std::abs(values[0]) > 4 || std::abs(values[1]) > 4 || std::abs(values[2]) > 4 || values[3] < 0 || values[3] > 1) return Result::malformed;
    frame.joints[n] = {values[0], values[1], values[2], values[3]};
  }
  frame.received = std::chrono::steady_clock::now();
  std::lock_guard lock(mutex);
  if (!enabled.load()) return Result::disabled;
  if (frame.session == latest.session && frame.sequence <= latest.sequence) return Result::replay;
  latest = frame; ++packets;
  return Result::accepted;
}
inline Frame Snapshot() {
  if (!enabled.load()) return {};
  std::lock_guard lock(mutex);
  auto frame = latest;
  if (std::chrono::steady_clock::now() - frame.received > std::chrono::milliseconds(500)) frame.tracked = false;
  return frame;
}
inline std::string Status() {
  std::lock_guard lock(mutex);
  const auto age = latest.sequence ? std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - latest.received).count() : -1;
  return std::string("{\"enabled\":") + (enabled.load() ? "true" : "false") +
    ",\"tracked\":" + (enabled.load() && latest.tracked && age >= 0 && age <= 500 ? "true" : "false") +
    ",\"packets\":" + std::to_string(packets) + ",\"sequence\":" + std::to_string(latest.sequence) +
    ",\"age_ms\":" + std::to_string(age) + ",\"guest_sensor_ready\":false,\"guest_camera_open\":" + (guest_camera_open.load() ? "true" : "false") +
    ",\"guest_depth_frames\":" + std::to_string(guest_depth_frames.load()) +
    ",\"guest_color_frames\":" + std::to_string(guest_color_frames.load()) +
    ",\"guest_completed_requests\":" + std::to_string(guest_completed_requests.load()) +
    ",\"guest_last_request\":" + std::to_string(guest_last_request.load()) +
    ",\"guest_skeleton_bound\":" + (guest_skeleton_bound.load() ? "true" : "false") +
    ",\"guest_skeleton_frames\":" + std::to_string(guest_skeleton_frames.load()) +
    ",\"guest_skeleton_bodies\":" + std::to_string(guest_skeleton_bodies.load()) + "}";
}
}
