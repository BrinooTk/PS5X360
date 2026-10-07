// SPDX-License-Identifier: MIT
// Host check of the skeleton frame layout and of the search for the title's
// NuiSkeletonGetNextFrame. With a guest image as the first argument (a private
// dump, never part of the repository) it also checks the search on real code:
//   check-virtual-skeleton [image.bin base-hex expected-hex]
#include "xbox360ps5/virtual_skeleton.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
using namespace xbox360ps5::motion;

static uint32_t U32At(const uint8_t* at) {
  return uint32_t(at[0]) << 24 | uint32_t(at[1]) << 16 | uint32_t(at[2]) << 8 | at[3];
}
static float FloatAt(const uint8_t* at) {
  const uint32_t bits = U32At(at);
  float value;
  std::memcpy(&value, &bits, 4);
  return value;
}
static void Put(std::vector<uint8_t>& code, size_t at, uint32_t word) {
  StoreU32(code.data() + at, word);
}

int main(int argc, char** argv) {
  std::vector<uint8_t> frame(kSkeletonFrameBytes + 16, 0xEE);
  Frame pose;
  // No body: every skeleton empty, identity fields as the library leaves them.
  WriteSkeletonFrame(frame.data(), pose, 7, 0x100000002ull, 5);
  assert(U32At(&frame[0]) == 1 && U32At(&frame[4]) == 2 && U32At(&frame[8]) == 7);
  assert(FloatAt(&frame[0x14]) == 1 && FloatAt(&frame[0x1C]) == kSensorHeight);
  assert(FloatAt(&frame[0x24]) == 1 && FloatAt(&frame[0x2C]) == 0);
  for (uint32_t n = 0; n < kSkeletonCount; ++n) {
    const uint8_t* body = &frame[kSkeletonDataOffset + n * kSkeletonDataBytes];
    assert(U32At(body) == 0 && U32At(body + 4) == 0);
    assert(U32At(body + 8) == kEnrollmentUnknown && U32At(body + 12) == kUserIndexNone);
  }
  // Nothing is written past the frame.
  for (size_t n = kSkeletonFrameBytes; n < frame.size(); ++n) assert(frame[n] == 0xEE);
  assert(kSkeletonDataOffset + kSkeletonCount * kSkeletonDataBytes == kSkeletonFrameBytes);

  // A body: the right hand (joint 11) raised, to the left of the picture and
  // towards the camera; the left foot (joint 15) badly seen.
  pose.tracked = true;
  for (auto& joint : pose.joints) joint.confidence = 1;
  pose.joints[1] = {0, .2f, 0, 1};
  pose.joints[3] = {0, .65f, 0, 1};
  pose.joints[11] = {-.4f, .9f, .3f, 1};
  pose.joints[15] = {.1f, -.9f, 0, .2f};
  WriteSkeletonFrame(frame.data(), pose, 8, 33, 5);
  const uint8_t* body = &frame[kSkeletonDataOffset];
  assert(U32At(body) == kSkeletonTracked && U32At(body + 4) == 5);
  assert(FloatAt(body + 0x14) == .2f + kHipHeight - kSensorHeight && FloatAt(body + 0x1C) == 1);
  const uint8_t* hand = body + 0x20 + 11 * 16;
  // The player's right is +x, up is +y, nearer the sensor is a smaller z.
  assert(FloatAt(hand) == .4f && FloatAt(hand + 8) == kBodyDistance - .3f && FloatAt(hand + 12) == 1);
  const uint8_t* head = body + 0x20 + 3 * 16;
  assert(FloatAt(hand + 4) > FloatAt(head + 4));
  assert(U32At(body + 0x160 + 11 * 4) == kJointTracked);
  assert(U32At(body + 0x160 + 15 * 4) == kJointInferred);
  // The feet stand on the floor plane within a few centimetres.
  const float foot = FloatAt(body + 0x20 + 15 * 16 + 4);
  assert(foot + kSensorHeight > -0.05f && foot + kSensorHeight < 0.05f);
  for (uint32_t n = 1; n < kSkeletonCount; ++n)
    assert(U32At(&frame[kSkeletonDataOffset + n * kSkeletonDataBytes]) == 0);
  // Tracking lost, or no identifier: no body in the frame.
  WriteSkeletonFrame(frame.data(), pose, 9, 66, 0);
  assert(U32At(&frame[kSkeletonDataOffset]) == 0);

  // The search: a function with all four marks is found once; the same code
  // without one mark, or the copy alone, is not.
  std::vector<uint8_t> code(0x1000, 0);
  for (size_t n = 0; n < code.size(); n += 4) Put(code, n, 0x60000000);  // nop
  Put(code, 0x100, 0x7D8802A6);  // mflr r12
  Put(code, 0x120, 0x28831F40);  // cmplwi cr1, r3, 0x1F40
  Put(code, 0x140, 0x1D4BD8F0);  // mulli r10, r11, -10000
  Put(code, 0x180, 0x38A00AB0);  // li r5, 0xAB0
  Put(code, 0x1C0, 0x616A048F);  // ori r10, r11, 0x48F
  Put(code, 0x800, 0x7D8802A6);
  Put(code, 0x840, 0x38A00AB0);
  auto found = FindSkeletonGetNextFrame(code.data(), code.size(), 0x82000000);
  assert(found.size() == 1 && found[0] == 0x82000100);
  Put(code, 0x140, 0x60000000);
  assert(FindSkeletonGetNextFrame(code.data(), code.size(), 0x82000000).empty());
  Put(code, 0x140, 0x1D4BD8F0);
  Put(code, 0x120, 0x28841F40);  // the limit on another register's value
  assert(FindSkeletonGetNextFrame(code.data(), code.size(), 0x82000000).empty());
  assert(FindSkeletonGetNextFrame(code.data(), 2, 0x82000000).empty());

  if (argc == 4) {
    std::ifstream file(argv[1], std::ios::binary);
    const std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    assert(!image.empty());
    const uint32_t base = uint32_t(std::strtoul(argv[2], nullptr, 16));
    found = FindSkeletonGetNextFrame(image.data(), image.size(), base);
    std::printf("guest image: %zu match(es)", found.size());
    for (uint32_t address : found) std::printf(" %08X", address);
    std::puts("");
    assert(found.size() == 1 && found[0] == uint32_t(std::strtoul(argv[3], nullptr, 16)));
  }
  std::puts("PASS: skeleton frame layout, sensor-space axes, joint states, bounds and function search");
}
