// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
namespace xbox360ps5 {
// A title override; restored automatically when the next executable is loaded.
inline std::atomic<bool> patch_vsync_off{false};
inline bool EffectiveVsync(bool preference) {
  return preference && !patch_vsync_off.load(std::memory_order_relaxed);
}
}
