// SPDX-License-Identifier: MIT
// An achievement a game has just unlocked for the first time, handed from the
// emulator core to the title, which shows it as a console notification. Called
// on the guest thread that earned it: the receiver must only queue.
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string>
namespace xbox360ps5 {
struct EarnedAchievement {
  std::string name, description;
  uint64_t xuid = 0;
  uint32_t gamerscore = 0, title_id = 0, id = 0;
  std::span<const uint8_t> icon;  // A PNG, valid during the call only; may be empty.
};
inline std::function<void(const EarnedAchievement&)> achievement_earned;
}
