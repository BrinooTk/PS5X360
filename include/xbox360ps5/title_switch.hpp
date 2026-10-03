// SPDX-License-Identifier: MIT
// Switching titles by restarting the emulator. A game that launches another
// executable (a collection's menu starting one of its games) or returns to the
// dashboard ends the whole title here: upstream's in-process switch needs a
// host stack walker that only exists on Windows. The next start of the title
// mounts the same game again and runs the requested executable directly, with
// the launch data it was given.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace xbox360ps5 {
struct PendingLaunch {
  std::filesystem::path game;  // What was launched: a .xex, an .iso or a package.
  std::string module;          // The guest executable to run in it, game:\...
  std::vector<uint8_t> launch_data;
};
// The game the emulator launched, mounted again after a switch.
void SetLaunchedGame(const std::filesystem::path& game);
// Records the request and restarts the title. guest_path empty: back to the
// launcher. Returns only when the restart is refused (and on the PC, unless
// XBOX360PS5_SWITCH_EXIT is set: then the process ends there, for tests).
bool RelaunchInto(const std::string& guest_path, const std::vector<uint8_t>& launch_data);
// At start-up: the switch a previous run asked for, once.
bool TakePendingLaunch(PendingLaunch& launch);
// The executable CompleteLaunch runs instead of the game's default, once.
void SetModuleOverride(const std::string& module);
std::string TakeModuleOverride();
// Per-game settings in Xenia's game config format (<TITLEID>.config.toml, keys
// such as GPU.readback_resolve): first the ones shipped with the title in
// assets/game-configs, then the user's in /download0/xbox360ps5/game-configs.
void LoadGameConfigs(const std::string& title_id);
}
