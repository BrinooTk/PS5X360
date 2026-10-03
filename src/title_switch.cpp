// SPDX-License-Identifier: MIT
#include "xbox360ps5/title_switch.hpp"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <utility>
namespace config {
void ReadGameConfig(const std::filesystem::path& file_path);  // config.cc, not in config.h.
}
#if XE_PLATFORM_PS5
extern "C" int sceSystemServiceLoadExec(const char*, const char**);
#endif
namespace xbox360ps5 {
namespace {
namespace fs = std::filesystem;
fs::path launched_game;
std::string module_override;
fs::path PendingFile() {
#if XE_PLATFORM_PS5
  return "/download0/xbox360ps5/next-title.txt";
#else
  return fs::temp_directory_path() / "xbox360ps5-next-title.txt";
#endif
}
}

void SetLaunchedGame(const fs::path& game) { launched_game = game; }

bool RelaunchInto(const std::string& guest_path, const std::vector<uint8_t>& launch_data) {
  std::error_code error;
  fs::remove(PendingFile(), error);
  if (!guest_path.empty() && !launched_game.empty()) {
    // Three lines: the game to mount, the executable in it, the launch data in hex.
    std::ofstream output(PendingFile(), std::ios::trunc);
    output << launched_game.string() << "\n" << guest_path << "\n";
    static const char* const hex = "0123456789abcdef";
    for (uint8_t byte : launch_data) output << hex[byte >> 4] << hex[byte & 15];
    output << "\n";
    output.close();
    XELOGW("Title switch: restarting into {} ({} bytes of launch data)", guest_path, launch_data.size());
  } else {
    XELOGW("Title switch: the game returned to the dashboard; restarting into the launcher");
  }
#if XE_PLATFORM_PS5
  {
    // Called from a guest thread while others still log: the logger is left running.
    const int refused = sceSystemServiceLoadExec("/app0/eboot.bin", nullptr);
    XELOGE("Title switch: restart refused {:08X}", unsigned(refused));
  }
#else
  if (std::getenv("XBOX360PS5_SWITCH_EXIT")) {
    std::fflush(nullptr);
    std::_Exit(0);
  }
#endif
  return false;
}

bool TakePendingLaunch(PendingLaunch& launch) {
  std::ifstream input(PendingFile());
  std::string game, data;
  if (!std::getline(input, game) || !std::getline(input, launch.module) || game.empty()) return false;
  std::getline(input, data);
  input.close();
  std::error_code error;
  fs::remove(PendingFile(), error);
  launch.game = game;
  launch.launch_data.clear();
  for (size_t at = 0; at + 1 < data.size(); at += 2)
    launch.launch_data.push_back(uint8_t(std::stoul(data.substr(at, 2), nullptr, 16)));
  return fs::exists(launch.game, error);
}

void SetModuleOverride(const std::string& module) { module_override = module; }

std::string TakeModuleOverride() { return std::exchange(module_override, std::string()); }

void LoadGameConfigs(const std::string& title_id) {
#if XE_PLATFORM_PS5
  const fs::path folders[] = {"/app0/assets/game-configs", "/download0/xbox360ps5/game-configs"};
#else
  const fs::path folders[] = {"dist/PPSA50011/assets/game-configs"};
#endif
  for (const auto& folder : folders) {
    const fs::path file = folder / (title_id + ".config.toml");
    std::error_code error;
    if (!fs::is_regular_file(file, error)) continue;
    XELOGW("Game config: {}", file.string());
    config::ReadGameConfig(file);
  }
}
}
