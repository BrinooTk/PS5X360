// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace xbox360ps5 {
// Entries and statistics belong to the GPU command thread. Other threads only
// invalidate the atomic epoch while holding the shared-memory critical region.
class ValidatedRangeCache {
 public:
  struct Stats { uint64_t requests, hits; };
  uint64_t Epoch() const { return epoch_.load(std::memory_order_acquire); }
  void Invalidate() { epoch_.fetch_add(1, std::memory_order_release); }
  bool Hit(uint32_t start, uint32_t length, uint64_t epoch) {
    ++requests_;
    const auto& e = entries_[Index(start, length)];
    if (epoch && e.epoch == epoch && e.start == start && e.length == length) {
      ++hits_; return true;
    }
    return false;
  }
  void Remember(uint32_t start, uint32_t length, uint64_t validated_epoch) {
    // Never publish a validation that raced with a guest write or cache reset.
    if (validated_epoch && Epoch() == validated_epoch)
      entries_[Index(start, length)] = {start, length, validated_epoch};
  }
  Stats TakeStats() { Stats s{requests_, hits_}; requests_ = hits_ = 0; return s; }
 private:
  struct Entry { uint32_t start = 0, length = 0; uint64_t epoch = 0; };
  static unsigned Index(uint32_t start, uint32_t length) {
    return ((start >> 4) ^ (start >> 12) ^ (length * 2654435761u)) & 63;
  }
  std::atomic<uint64_t> epoch_{1};
  std::array<Entry, 64> entries_{};
  uint64_t requests_ = 0, hits_ = 0;
};
}
