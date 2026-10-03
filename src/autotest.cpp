// SPDX-License-Identifier: MIT
#include "xbox360ps5/autotest.hpp"
#include "xbox360ps5/crash_report.hpp"
#include "xenia/base/logging.h"
#include "xenia/ui/presenter.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "third_party/stb/stb_image_write.h"
namespace xbox360ps5 {
namespace {
const char* const kRequest = "/app0/assets/autotest.txt";
const char* const kState = "/download0/xbox360ps5/autotest-state.txt";
void Collect(void* context, void* data, int size) {
  auto* bytes = static_cast<std::vector<uint8_t>*>(context);
  bytes->insert(bytes->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
}
}

bool AutoTest::Begin(const std::vector<std::filesystem::path>& games) {
  std::ifstream request(kRequest);
  if (!request) return false;
  // "<seconds per game> <run name>": a new run name starts again from the first game.
  std::string run;
  request >> seconds >> run;
  if (seconds < 10 || seconds > 3600) seconds = 60;
  std::ifstream state(kState);
  std::string state_run;
  if (!(state >> state_run >> index) || state_run != run) index = 0;
  count = int(games.size());
  if (index >= count) {
    XELOGW("AUTOTEST finished: {} games", count);
    return false;
  }
  std::ofstream(kState, std::ios::trunc) << run << " " << index + 1 << "\n";
  active = true;
  XELOGW("AUTOTEST game {} of {}: {} ({} s)", index + 1, count, games[size_t(index)].string(), seconds);
  return true;
}

uint32_t AutoTest::Buttons(double elapsed) const {
  const double phase = std::fmod(elapsed, 4.0);
  if (phase < 0.12) return 0x08;                    // OPTIONS: START.
  if (phase >= 2.0 && phase < 2.12) return 0x4000;  // Cross: A.
  return 0;
}

void AutoTest::SendShot(const xe::ui::RawImage& image, int index, const std::string& tag) {
  if (!image.width || !image.height) return;
  // Half size is enough to judge a picture and keeps the line small.
  const uint32_t width = image.width / 2, height = image.height / 2;
  std::vector<uint8_t> pixels(size_t(width) * height * 3);
  for (uint32_t y = 0; y < height; ++y)
    for (uint32_t x = 0; x < width; ++x)
      for (int c = 0; c < 3; ++c)
        pixels[(size_t(y) * width + x) * 3 + c] = image.data[size_t(y) * 2 * image.stride + size_t(x) * 2 * 4 + c];
  std::vector<uint8_t> png;
  stbi_write_png_to_func(Collect, &png, int(width), int(height), 3, pixels.data(), int(width * 3));
  static const char* const digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string line = "[X360] SHOT " + std::to_string(index) + " " + tag + " ";
  for (size_t at = 0; at < png.size(); at += 3) {
    const uint32_t chunk = uint32_t(png[at]) << 16 | (at + 1 < png.size() ? uint32_t(png[at + 1]) << 8 : 0) |
                           (at + 2 < png.size() ? png[at + 2] : 0);
    line += digits[chunk >> 18 & 63];
    line += digits[chunk >> 12 & 63];
    line += at + 1 < png.size() ? digits[chunk >> 6 & 63] : '=';
    line += at + 2 < png.size() ? digits[chunk & 63] : '=';
  }
  line += "\n";
  NetLog(line.data(), line.size());
}
}
