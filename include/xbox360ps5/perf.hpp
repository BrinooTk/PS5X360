// SPDX-License-Identifier: MIT
// Frames the game hands to the display, counted where the GPU command
// processor executes the guest's swap packet.
#pragma once
#include <atomic>
#include <cstdint>
namespace xbox360ps5 {
inline std::atomic<uint64_t> guest_frames{0};
inline void CountGuestFrame() { guest_frames.fetch_add(1, std::memory_order_relaxed); }
}
