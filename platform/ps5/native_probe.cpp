// SPDX-License-Identifier: MIT
// M1 hardware probe: actual Xenia decoding, VideoOut, input, and local logs.
#include "demo_renderer.hpp"
#include "probe_cases.hpp"
#include <array>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
int sceUserServiceInitialize(void*);
int sceUserServiceGetInitialUser(int*);
int sceUserServiceGetLoginUserIdList(int*);
int scePadInit();
int scePadOpen(int, int, int, void*);
int scePadGetHandle(int, int, int);
int scePadReadState(int, void*);
}

namespace {
int pad = -1;
unsigned previous_buttons = 0, cross_presses = 0, runs = 0, failures = 0;
std::array<bool, xbox360ps5::decoder_case_count> results{};
#ifdef XBOX360PS5_TRANSLATION_PROBE
unsigned translation_cases = 0, translation_failures = 0;
#endif

void trace(const char* text) noexcept {
  const int fd = open("/download0/xbox360ps5-m1.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
  if (fd >= 0) {
    (void)fchmod(fd, 0644);
    (void)write(fd, text, __builtin_strlen(text));
    (void)write(fd, "\n", 1);
    (void)close(fd);
  }
}

void run_decoder() noexcept {
  failures = 0;
  ++runs;
  for (unsigned i = 0; i < results.size(); ++i) {
    results[i] = xbox360ps5::check(xbox360ps5::decoder_cases[i]);
    failures += !results[i];
  }
  char line[96];
  std::snprintf(line, sizeof(line), "M1 decoder runs=%u cases=%u failures=%u",
                runs, xbox360ps5::decoder_case_count, failures);
  trace(line);
#ifdef XBOX360PS5_TRANSLATION_PROBE
  extern unsigned translation_run(unsigned&);
  translation_failures = translation_run(translation_cases);
  std::snprintf(line, sizeof(line), "M4 translation runs=%u cases=%u failures=%u",
                runs, translation_cases, translation_failures);
  trace(line);
#endif
}

int open_pad() noexcept {
  (void)sceUserServiceInitialize(nullptr);
  (void)scePadInit();
  int initial = -1;
  (void)sceUserServiceGetInitialUser(&initial);
  int users[4] = {-1, -1, -1, -1};
  (void)sceUserServiceGetLoginUserIdList(users);
  const int candidates[] = {initial, users[0], users[1], users[2], users[3], 0xff};
  for (const int user : candidates) {
    if (user < 0) continue;
    int result = scePadOpen(user, 0, 0, nullptr);
    if (result < 0) result = scePadGetHandle(user, 0, 0);
    if (result >= 0) return result;
  }
  return -1;
}

void draw(ps5::demo::Canvas& canvas) noexcept {
  using ps5::demo::Color;
  alignas(16) unsigned char sample[128]{};
  const int read_result = pad >= 0 ? scePadReadState(pad, sample) : -1;
  const bool connected = read_result >= 0 && sample[76] != 0;
  const unsigned buttons = connected ? (unsigned(sample[0]) | unsigned(sample[1]) << 8 |
      unsigned(sample[2]) << 16 | unsigned(sample[3]) << 24) : 0;
  if ((buttons & ~previous_buttons) & 0x4000) {
    ++cross_presses;
    run_decoder();
  }
  previous_buttons = buttons;
  canvas.clear(Color::background);
  canvas.rectangle(80, 70, 1760, 940, Color::panel);
  canvas.text(130, 110, "XBOX360PS5 M1", 7, Color::cyan);
  canvas.text(130, 185, "XENIA PLATFORM TEST", 4, Color::white);
  canvas.text(130, 240, "GAME EMULATION IS NOT AVAILABLE YET", 3, Color::yellow);
  for (unsigned i = 0; i < results.size(); ++i) {
    char line[100];
    std::snprintf(line, sizeof(line), "%s %s", results[i] ? "PASS" : "FAIL",
                  xbox360ps5::decoder_cases[i].name);
    canvas.text(130, 320 + i * 42, line, 3, results[i] ? Color::cyan : Color::yellow);
  }
  char summary[96];
  std::snprintf(summary, sizeof(summary), "CASES %u FAILURES %u RUNS %u",
                xbox360ps5::decoder_case_count, failures, runs);
  canvas.text(950, 330, summary, 3, Color::white);
  canvas.text(950, 415, connected ? "DUALSENSE READY" : "PAD NOT CONNECTED", 3,
              connected ? Color::cyan : Color::yellow);
  std::snprintf(summary, sizeof(summary), "CROSS PRESSES %u", cross_presses);
  canvas.text(950, 475, summary, 3, Color::white);
  std::snprintf(summary, sizeof(summary), "LEFT STICK %u %u", sample[4], sample[5]);
  canvas.text(950, 535, summary, 3, Color::white);
#ifdef XBOX360PS5_TRANSLATION_PROBE
  std::snprintf(summary, sizeof(summary), "PPC HIR CASES %u", translation_cases);
  canvas.text(950, 615, summary, 3, Color::cyan);
  std::snprintf(summary, sizeof(summary), "PPC FAILURES %u", translation_failures);
  canvas.text(950, 675, summary, 3, translation_failures ? Color::yellow : Color::cyan);
#endif
  canvas.text(130, 825, "PRESS X TO RUN THE DECODER TESTS AGAIN", 3, Color::white);
  canvas.text(130, 900, "CLOSE THIS TEST FROM THE PS5 HOME SCREEN", 3, Color::white);
}
}  // namespace

#ifdef XBOX360PS5_TRANSLATION_PROBE
unsigned run_ppc_translation_probe(unsigned&);
namespace {
unsigned translation_run(unsigned& cases) { return run_ppc_translation_probe(cases); }
}
#endif

int main() {
  trace("M1 start native title PPSA50008; no JIT or Vulkan initialization");
  run_decoder();
  pad = open_pad();
  char message[96];
  std::snprintf(message, sizeof(message), "M1 pad handle=%d", pad);
  trace(message);
  ps5::demo::run(draw, "Xbox360PS5 M1: native platform test ready");
}
