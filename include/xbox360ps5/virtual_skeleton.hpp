// SPDX-License-Identifier: MIT
// Research skeleton source: the estimated pose in the layout a title's NUI
// library returns from NuiSkeletonGetNextFrame. The title's own body tracking
// runs on the console GPU with copy commands the emulator does not have, so its
// result is replaced, not helped. Estimated joints are not sensor measurements.
#pragma once
#include "xbox360ps5/motion_input.hpp"
#include <algorithm>
#include <cstddef>
#include <vector>
namespace xbox360ps5::motion {
// NUI_SKELETON_FRAME of the Xbox 360 XDK, big-endian. The 0xAB0 size and the
// field offsets below are the ones the library and the game of title 4E4D084E
// use (the library copies 0xAB0 bytes, the game walks 0x1C0 per skeleton).
constexpr size_t kSkeletonFrameBytes = 0xAB0;
constexpr size_t kSkeletonDataOffset = 0x30, kSkeletonDataBytes = 0x1C0;
constexpr uint32_t kSkeletonCount = 6, kJointCount = 20;
constexpr uint32_t kSkeletonTracked = 2, kJointInferred = 1, kJointTracked = 2;
// What the library writes for a body that the identity service has not
// recognized: no enrolment, no signed-in user.
constexpr uint32_t kEnrollmentUnknown = 0xFFFFFFFF, kUserIndexNone = 0xFE;
// Placement of the estimated body in front of the virtual sensor, in metres.
// The pose is relative to the hips, so the body does not walk around.
constexpr float kSensorHeight = 0.8f, kHipHeight = 0.9f, kBodyDistance = 2.5f;

inline void StoreU32(uint8_t* at, uint32_t value) {
  at[0] = uint8_t(value >> 24); at[1] = uint8_t(value >> 16);
  at[2] = uint8_t(value >> 8); at[3] = uint8_t(value);
}
inline void StoreVector(uint8_t* at, float x, float y, float z, float w) {
  const float values[4] = {x, y, z, w};
  for (size_t n = 0; n < 4; ++n) {
    uint32_t bits;
    std::memcpy(&bits, &values[n], 4);
    StoreU32(at + n * 4, bits);
  }
}
// Sensor space: metres, origin at the sensor, +x to the sensor's left (the
// right side of a player who faces it), +y up, +z away from the sensor. The
// bridge sends +x to the right of the picture and +z towards the camera.
inline void SkeletonSpace(const Joint& joint, float& x, float& y, float& z) {
  x = -joint.x;
  y = joint.y + kHipHeight - kSensorHeight;
  z = std::clamp(kBodyDistance - joint.z, 0.8f, 4.0f);
}
// Fills one frame. `tracking_id` names the body for as long as it is followed;
// 0 writes a frame without a body.
inline void WriteSkeletonFrame(uint8_t* frame, const Frame& pose,
                               uint32_t frame_number, uint64_t milliseconds,
                               uint32_t tracking_id) {
  std::memset(frame, 0, kSkeletonFrameBytes);
  StoreU32(frame + 0x00, uint32_t(milliseconds >> 32));
  StoreU32(frame + 0x04, uint32_t(milliseconds));
  StoreU32(frame + 0x08, frame_number);
  // Floor plane y + height = 0 and gravity straight down: a level sensor.
  StoreVector(frame + 0x10, 0, 1, 0, kSensorHeight);
  StoreVector(frame + 0x20, 0, 1, 0, 0);
  for (uint32_t n = 0; n < kSkeletonCount; ++n) {
    uint8_t* body = frame + kSkeletonDataOffset + n * kSkeletonDataBytes;
    StoreU32(body + 0x08, kEnrollmentUnknown);
    StoreU32(body + 0x0C, kUserIndexNone);
  }
  if (!pose.tracked || !tracking_id) return;
  uint8_t* body = frame + kSkeletonDataOffset;
  StoreU32(body + 0x00, kSkeletonTracked);
  StoreU32(body + 0x04, tracking_id);
  float x, y, z;
  // The library reports the centre of the body; the spine joint stands for it.
  SkeletonSpace(pose.joints[1], x, y, z);
  StoreVector(body + 0x10, x, y, z, 1);
  for (uint32_t n = 0; n < kJointCount; ++n) {
    SkeletonSpace(pose.joints[n], x, y, z);
    StoreVector(body + 0x20 + n * 16, x, y, z, 1);
    // A joint the camera does not see well stays in the skeleton as inferred,
    // as the sensor does for a hidden limb.
    StoreU32(body + 0x160 + n * 4,
             pose.joints[n].confidence >= 0.5f ? kJointTracked : kJointInferred);
  }
}

// Finds NuiSkeletonGetNextFrame in a title's code by what only that function
// does together: it limits the wait to 8000 ms, turns milliseconds into 100 ns
// units, copies one 0xAB0-byte frame and can answer "device not connected".
// Registers differ between builds, so only opcodes and constants are compared.
// Returns every match; a caller must refuse anything but exactly one.
inline std::vector<uint32_t> FindSkeletonGetNextFrame(const uint8_t* code,
                                                      size_t bytes,
                                                      uint32_t base) {
  auto word = [&](size_t at) {
    return uint32_t(code[at]) << 24 | uint32_t(code[at + 1]) << 16 |
           uint32_t(code[at + 2]) << 8 | uint32_t(code[at + 3]);
  };
  constexpr uint32_t kProlog = 0x7D8802A6;      // mflr r12
  constexpr uint32_t kCopyFrame = 0x38A00AB0;   // li r5, 0xAB0
  constexpr size_t kFunctionSpan = 0x280;
  std::vector<uint32_t> found;
  for (size_t at = 0; at + 4 <= bytes; at += 4) {
    if (word(at) != kCopyFrame) continue;
    size_t start = at;
    while (start >= 4 && at - start < kFunctionSpan && word(start) != kProlog) start -= 4;
    if (word(start) != kProlog) continue;
    bool wait_limit = false, to_ticks = false, not_connected = false;
    for (size_t n = start; n < start + kFunctionSpan && n + 4 <= bytes; n += 4) {
      const uint32_t w = word(n), op = w >> 26, low = w & 0xFFFF;
      // cmplwi crN, r3, 0x1F40: the wait is the function's first argument.
      wait_limit |= op == 10 && ((w >> 16) & 31) == 3 && low == 0x1F40;
      to_ticks |= op == 7 && low == 0xD8F0;        // mulli ?, ?, -10000
      not_connected |= op == 24 && low == 0x048F;  // ori ?, ?, 0x48F (8007048F)
    }
    if (wait_limit && to_ticks && not_connected &&
        std::find(found.begin(), found.end(), base + uint32_t(start)) == found.end())
      found.push_back(base + uint32_t(start));
  }
  return found;
}
}
