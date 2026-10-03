// SPDX-License-Identifier: MIT
// Unattended compatibility run on the console. When assets/autotest.txt is in
// the title folder, each start of the title plays the next game of the library
// for a while, pressing START and A by itself, sends screenshots and frame
// rates over the log stream, and restarts into the following game.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace xe::ui { struct RawImage; }
namespace xbox360ps5 {
struct AutoTest {
  bool active = false;
  int seconds = 60;          // How long each game runs.
  int index = 0;             // The game this start plays.
  int count = 0;
  // Reads the request and advances the saved position before the game runs,
  // so a game that crashes the title does not stop the run.
  bool Begin(const std::vector<std::filesystem::path>& games);
  // START, then A two seconds later, every four seconds.
  uint32_t Buttons(double elapsed_seconds) const;
  // A screenshot as one log line: "[X360] SHOT <index> <tag> <base64 png>".
  static void SendShot(const xe::ui::RawImage& image, int index, const std::string& tag);
};
}
