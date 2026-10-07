// SPDX-License-Identifier: MIT
// Console notifications from the title. Callers only queue: a worker thread
// talks to the system, so game and interface threads never wait on it.
//
// A notification is tried as the system's own toast with a picture and the
// trophy sound (libSceNotification, loaded on first use: a console that will
// not load it for a title must not stop the title from starting). When that
// library or its call is refused, the text goes out as the plain system
// message every firmware accepts.
#pragma once
#include <cstdint>
#include <span>
#include <string>
namespace xbox360ps5 {
struct Toast {
  std::string title, text;
  std::string icon_path;  // A PNG the system can read (/data/...), or empty.
  bool trophy_sound = false;
};
void StartNotifier(int user_id);
void Notify(Toast toast);
// Keeps an achievement's picture where the system can read it. Empty on failure.
std::string SaveAchievementIcon(uint32_t title_id, uint32_t achievement_id, std::span<const uint8_t> png);
}
