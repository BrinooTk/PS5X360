// SPDX-License-Identifier: MIT
// The interface's sound effects: short clips mixed into an audio port of
// their own, apart from the emulated console's sound.
#include "xbox360ps5/ui_sounds.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
extern "C" {
int sceAudioOutInit();
int sceAudioOutOpen(int32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t);
int sceAudioOutOutput(int32_t, const void*);
}
namespace xbox360ps5 {
namespace {
constexpr uint32_t kGrain = 256;  // Samples per channel in one port output.
constexpr const char* kFiles[] = {"move", "select", "back"};
// 48 kHz, 16-bit stereo, as tools made them from the user's recordings.
std::vector<int16_t> clips[std::size(kFiles)];
struct Voice { int clip; size_t at; };
std::mutex mutex;
std::vector<Voice> voices;
std::atomic<bool> muted{false};
bool started = false;

void Run(int port) {
  std::array<int16_t, kGrain * 2> grain;
  for (;;) {
    std::array<int32_t, kGrain * 2> mix{};
    {
      std::lock_guard lock(mutex);
      for (auto voice = voices.begin(); voice != voices.end();) {
        const auto& clip = clips[voice->clip];
        const size_t count = std::min(mix.size(), clip.size() - voice->at);
        for (size_t n = 0; n < count; ++n) mix[n] += clip[voice->at + n];
        voice->at += count;
        voice = voice->at >= clip.size() ? voices.erase(voice) : voice + 1;
      }
    }
    for (size_t n = 0; n < mix.size(); ++n) grain[n] = int16_t(std::clamp(mix[n], -32768, 32767));
    // The output call waits for the port, which paces this loop.
    if (sceAudioOutOutput(port, grain.data()) < 0) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
}
void StartUiSounds() {
  if (started) return;
  started = true;
  bool any = false;
  for (size_t n = 0; n < std::size(kFiles); ++n) {
    std::ifstream file(std::string("/app0/assets/sounds/") + kFiles[n] + ".raw", std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    clips[n].resize(bytes.size() / 4 * 2);
    std::copy_n(bytes.data(), clips[n].size() * 2, reinterpret_cast<char*>(clips[n].data()));
    any |= !clips[n].empty();
  }
  if (!any) return;
  sceAudioOutInit();
  // The main port of the system user (255), 16-bit stereo: as the games' port.
  const int port = sceAudioOutOpen(0xff, 0, 0, kGrain, 48000, 1);
  if (port < 0) return;
  std::thread(Run, port).detach();
}
void PlayUiSound(UiSound sound) {
  const int clip = int(sound);
  if (muted || clips[clip].empty()) return;
  std::lock_guard lock(mutex);
  // A sound that is still playing starts again instead of piling up.
  for (auto& voice : voices) if (voice.clip == clip) { voice.at = 0; return; }
  voices.push_back({clip, 0});
}
void MuteUiSounds(bool mute) { muted = mute; }
}
