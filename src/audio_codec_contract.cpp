// SPDX-License-Identifier: MIT
// Verify actual decoder availability. This does not claim a game audio sample decoded.
extern "C" {
#include "libavcodec/avcodec.h"
}
#include <cstdio>
#include <initializer_list>
#include "xbox360ps5/native_audio.hpp"
#include "xenia/base/byte_order.h"
#include <array>
#include <limits>
int main() {
  for (auto id : {AV_CODEC_ID_XMA1, AV_CODEC_ID_XMA2, AV_CODEC_ID_XMAFRAMES}) {
    auto codec = avcodec_find_decoder(id);
    if (!codec) { std::puts("FAIL missing XMA decoder"); return 1; }
    auto context = avcodec_alloc_context3(codec);
    if (!context) return 1;
    std::printf("ACTUAL CODEC %s allocated (no game sample decoded)\n", codec->name);
    avcodec_free_context(&context);
    if (context) return 1;
  }
  std::array<float, 1536> input{};
  std::array<int16_t, 512> output{};
  for (unsigned i = 0; i < 256; ++i) input[i] = xe::byte_swap(0.5f);
  xbox360ps5::ConvertXboxAudioFrame(input.data(), output.data());
  for (unsigned i = 0; i < 256; ++i)
    if (output[i * 2] != 6553 || output[i * 2 + 1] != 0) return 1;
  input.fill(0);
  for (unsigned i = 0; i < 256; ++i) input[512 + i] = xe::byte_swap(1.0f);
  xbox360ps5::ConvertXboxAudioFrame(input.data(), output.data());
  for (auto sample : output) if (sample != 6553) return 1;
  input.fill(xe::byte_swap(10.0f));
  xbox360ps5::ConvertXboxAudioFrame(input.data(), output.data());
  for (auto sample : output) if (sample != 32767) return 1;
  input.fill(xe::byte_swap(std::numeric_limits<float>::quiet_NaN()));
  xbox360ps5::ConvertXboxAudioFrame(input.data(), output.data());
  for (auto sample : output) if (sample) return 1;
  std::puts("ACTUAL PCM CONVERSION 256 stereo frames, endian/channels/clipping/nonfinite PASS (no AudioOut hardware test)");
  return 0;
}
