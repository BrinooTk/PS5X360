"""Differential check of "unwatched pages": the production shared-memory code with the feature on
must leave the GPU copy of every requested range byte for byte as the same code with it off does,
through random guest writes, requests of both kinds, GPU writes, releases, watches and ageing."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
source = (root / '.deps/xenia-canary/src/xenia/gpu/shared_memory.cc').read_text()


def cut(start, end):
    a = source.index(start)
    return source[a:source.index(end, a)]


unwatched = cut('// Xbox360PS5 experimental: unwatched pages.', '#endif  // XE_PLATFORM_PS5')
written = cut('void SharedMemory::RangeWrittenByGpu(', 'bool SharedMemory::AllocateSparseHostGpuMemoryRange(')
valid = cut('void SharedMemory::MakeRangeValid(', 'void SharedMemory::UnlinkWatchRange(') if 'void SharedMemory::UnlinkWatchRange(' in source[source.index('void SharedMemory::MakeRangeValid('):] else None
if valid is None:
    a = source.index('void SharedMemory::MakeRangeValid(')
    valid = source[a:source.index('\n}\n', a) + 3]
else:
    a = source.index('void SharedMemory::MakeRangeValid(')
    valid = source[a:source.index('\n}\n', a) + 3]
request = cut('bool SharedMemory::RequestRange(', 'bool SharedMemory::IsRangeValid(')
find = cut('void SharedMemory::TryFindUploadRange(', 'std::pair<uint32_t, uint32_t> SharedMemory::MemoryInvalidationCallbackThunk(')
invalidate = cut('std::pair<uint32_t, uint32_t> SharedMemory::MemoryInvalidationCallback(\n', 'void SharedMemory::PrepareForTraceDownload()')
out = root / 'build/unwatched-pages-check'
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <utility>
#include <vector>
#include "xbox360ps5/gpu_diagnostics.hpp"
#include "xbox360ps5/validated_range_cache.hpp"
#define XE_PLATFORM_PS5 1
#define XE_NOINLINE
#define XE_FORCEINLINE inline
#define XE_RESTRICT
#define XE_LIKELY(x) (x)
#define XE_UNLIKELY(x) (x)
#define SCOPE_profile_cpu_f(x)
#define XELOGW(...)
namespace xe {
inline uint8_t lzcnt(uint64_t v) { return uint8_t(std::countl_zero(v)); }
inline uint8_t tzcnt(uint64_t v) { return uint8_t(std::countr_zero(v)); }
inline uint32_t bit_count(uint64_t v) { return uint32_t(std::popcount(v)); }
template <class T> inline bool bit_scan_forward(T v, uint32_t* out) { if (!v) return false; *out = uint32_t(std::countr_zero(v)); return true; }
inline void FatalError(const char*) { assert(false); }
}
// A shift whose count wraps at the word's width, as the core's helper.
inline uint64_t mod_shift_left(uint64_t value, uint32_t count) { return value << (count & 63); }
constexpr unsigned int MAX_UPLOAD_RANGES = 4096;
// Any hash of the bytes serves the logic under test.
inline uint64_t XXH3_64bits(const void* data, size_t size) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < size; ++i) h = (h ^ static_cast<const uint8_t*>(data)[i]) * 1099511628211ull;
  return h;
}
constexpr uint32_t kUnits = 512, kUnit = 4096, kHostUnits = 4;
struct SharedMemory;
struct Memory {
  SharedMemory* owner;
  uint8_t* TranslatePhysical(uint32_t address) const;
  void EnablePhysicalMemoryAccessCallbacks(uint32_t start, uint32_t length, bool, bool);
};
struct SharedMemory {
  static constexpr uint32_t kBufferSize = kUnits * kUnit;
  unsigned page_size_log2_ = 12;
  std::vector<uint8_t> cpu = std::vector<uint8_t>(kBufferSize), gpu = std::vector<uint8_t>(kBufferSize);
  std::array<bool, kUnits / kHostUnits> armed{};
  uint64_t valid_words[kUnits / 64]{}, written_words[kUnits / 64]{};
  uint64_t* system_page_flags_valid_ = valid_words;
  uint64_t* system_page_flags_valid_and_gpu_written_ = written_words;
  xbox360ps5::ValidatedRangeCache request_cache_;
  struct Ranges { std::array<std::pair<uint32_t, uint32_t>, 4096> items; void clear() {} std::pair<uint32_t, uint32_t>* data() { return items.data(); } } upload_ranges_;
  struct Lock { int Acquire() { return 0; } } global_critical_region_;
  Memory memory_{this};
  Memory& memory() { return memory_; }
  void* memory_invalidation_callback_handle_ = this;
  uint64_t upload_bytes_ = 0, upload_chunks_ = 0, lock_free_hits_ = 0, faults = 0, uploaded_units = 0;
  // The members of the feature under test.
  std::vector<uint8_t> unit_heat_ = std::vector<uint8_t>(kUnits);
  std::vector<uint64_t> unwatched_bits_ = std::vector<uint64_t>(kUnits / 64), unwatched_seen_bits_ = std::vector<uint64_t>(kUnits / 64),
      pinned_bits_ = std::vector<uint64_t>(kUnits / 64), unit_hash_ = std::vector<uint64_t>(kUnits);
  std::vector<std::pair<uint32_t, uint32_t>> unwatched_filtered_;
  bool request_allows_unwatched_ = false;
  uint64_t unwatched_skips_ = 0, unwatched_uploads_ = 0;
  std::atomic<uint64_t> unwatched_heated_{0}, unwatched_cooled_{0};
  bool UnwatchedPagesEnabled() const;
  void NoteCpuWrite(uint32_t, uint32_t);
  void ForgetUnwatched(uint32_t, uint32_t);
  void PinWatched(uint32_t, uint32_t);
  unsigned int FilterUnwatched(std::pair<uint32_t, uint32_t>*&, unsigned int);
  void AgeUnwatchedPages();
  void RangeWrittenByGpu(uint32_t, uint32_t);
  void MakeRangeValid(uint32_t, uint32_t, bool);
  bool RequestRange(uint32_t, uint32_t, bool allow_unwatched = false);
  void TryFindUploadRange(const uint32_t&, const uint32_t&, const uint32_t&, const uint32_t&, uint32_t&, unsigned int&, std::pair<uint32_t, uint32_t>*);
  void TryGetNextUploadRange(uint32_t&, uint64_t&, const uint32_t&, unsigned int&, std::pair<uint32_t, uint32_t>*);
  std::pair<uint32_t, uint32_t> MemoryInvalidationCallback(uint32_t, uint32_t, bool);
  bool EnsureHostGpuMemoryAllocated(uint32_t, uint32_t) { return true; }
  void FireWatches(uint32_t, uint32_t, bool) {}
  // As the Vulkan implementation: valid and watched first, then the copy.
  bool UploadRanges(const std::pair<uint32_t, uint32_t>* ranges, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
      MakeRangeValid(ranges[i].first << 12, ranges[i].second << 12, false);
      std::memcpy(&gpu[size_t(ranges[i].first) << 12], &cpu[size_t(ranges[i].first) << 12], size_t(ranges[i].second) << 12);
      uploaded_units += ranges[i].second;
    }
    return true;
  }
  // The guest writes: a watched host page faults first, as the memory system would have it.
  void GuestWrite(uint32_t address, uint32_t length, uint8_t value) {
    for (uint32_t page = address / (kUnit * kHostUnits); page <= (address + length - 1) / (kUnit * kHostUnits); ++page) {
      if (!armed[page]) continue;
      ++faults;
      const auto range = MemoryInvalidationCallback(page * kUnit * kHostUnits, kUnit * kHostUnits, false);
      armed[page] = false;
      for (uint32_t other = 0; other < armed.size(); ++other) {
        const uint32_t first = other * kUnit * kHostUnits;
        if (first >= range.first && first + kUnit * kHostUnits <= range.first + range.second) armed[other] = false;
      }
    }
    std::memset(&cpu[address], value, length);
  }
  void Release(uint32_t address, uint32_t length) {
    MemoryInvalidationCallback(address, length, true);
    // Only host pages wholly inside the range lose their watch.
    for (uint32_t page = 0; page < armed.size(); ++page) {
      const uint32_t first = page * kUnit * kHostUnits;
      if (first >= address && first + kUnit * kHostUnits <= address + length) armed[page] = false;
    }
  }
  void GpuWrite(uint32_t address, uint32_t length, uint8_t value) {
    RequestRange(address, length);  // As the contract asks before a GPU write.
    std::memset(&gpu[address], value, length);
    RangeWrittenByGpu(address, length);
  }
  uint32_t Unwatched() const { uint32_t n = 0; for (auto w : unwatched_bits_) n += uint32_t(std::popcount(w)); return n; }
};
uint8_t* Memory::TranslatePhysical(uint32_t address) const { return owner->cpu.data() + address; }
void Memory::EnablePhysicalMemoryAccessCallbacks(uint32_t start, uint32_t length, bool, bool) {
  for (uint32_t page = start / (kUnit * kHostUnits); page <= (start + length - 1) / (kUnit * kHostUnits); ++page) owner->armed[page] = true;
}
''' + unwatched + written + valid + request + find + invalidate + r'''
int main() {
  using namespace xbox360ps5;
  gpu_diag::enabled = true;
  gpu_diag::invalidation_window = 0x4000;
  // A unit the guest rewrites before every draw: watched, each write faults; left unwatched after three.
  {
    SharedMemory hot;
    gpu_diag::unwatched_pages = true;
    for (unsigned frame = 0; frame < 50; ++frame) {
      hot.GuestWrite(8 * kUnit + 16, 64, uint8_t(frame));
      assert(hot.RequestRange(8 * kUnit, 2 * kUnit, true));
      assert(!std::memcmp(&hot.gpu[8 * kUnit], &hot.cpu[8 * kUnit], 2 * kUnit));
      // A second draw from the same data in the frame uploads nothing.
      const uint64_t before = hot.uploaded_units;
      assert(hot.RequestRange(8 * kUnit, 2 * kUnit, true));
      assert(hot.uploaded_units == before);
    }
    std::printf("Rewritten every frame for 50 frames: %llu faults, %u units left unwatched\n", (unsigned long long)hot.faults, hot.Unwatched());
    assert(hot.faults <= 4 && hot.Unwatched() >= 1);
    // A request that keeps state (a texture) takes the unit back: valid, watched, pinned.
    assert(hot.RequestRange(8 * kUnit, kUnit, false));
    const auto unwatched8 = [&] { return bool(hot.unwatched_bits_[0] >> 8 & 1); };
    assert(!unwatched8() && hot.armed[2] && (hot.valid_words[0] >> 8 & 1) && (hot.pinned_bits_[0] >> 8 & 1));
    const uint64_t faults = hot.faults;
    for (unsigned frame = 0; frame < 20; ++frame) {
      hot.GuestWrite(8 * kUnit + 16, 64, uint8_t(100 + frame));
      assert(hot.RequestRange(8 * kUnit, kUnit, true));
      assert(!std::memcmp(&hot.gpu[8 * kUnit], &hot.cpu[8 * kUnit], kUnit));
    }
    assert(hot.faults == faults + 20 && !unwatched8());  // Pinned: watched for good.
    // Ageing returns an idle unwatched unit to being watched.
    SharedMemory idle;
    for (unsigned frame = 0; frame < 6; ++frame) { idle.GuestWrite(40 * kUnit, 8, uint8_t(frame)); assert(idle.RequestRange(40 * kUnit, kUnit, true)); }
    assert(idle.Unwatched() >= 1);
    for (unsigned n = 0; n < 8; ++n) idle.AgeUnwatchedPages();
    assert(idle.Unwatched() == 0);
    assert(idle.RequestRange(40 * kUnit, kUnit, true) && idle.armed[10]);
    // Switched off in the Guide: an unwatched unit is uploaded whole at its next request and is
    // watched again without being pinned; switched on again it can be left unwatched again.
    SharedMemory toggled;
    const auto unwatched16 = [&] { return bool(toggled.unwatched_bits_[0] >> 16 & 1); };
    for (unsigned frame = 0; frame < 6; ++frame) { toggled.GuestWrite(16 * kUnit, 8, uint8_t(frame + 1)); assert(toggled.RequestRange(16 * kUnit, kUnit, true)); }
    assert(unwatched16());
    gpu_diag::unwatched_pages = false;
    toggled.GuestWrite(16 * kUnit, 8, 77);
    assert(toggled.RequestRange(16 * kUnit, kUnit, true));
    assert(!unwatched16() && (toggled.valid_words[0] >> 16 & 1) && toggled.armed[4] && !(toggled.pinned_bits_[0] >> 16 & 1));
    assert(!std::memcmp(&toggled.gpu[16 * kUnit], &toggled.cpu[16 * kUnit], kUnit));
    const uint64_t faults_while_off = toggled.faults, uploads_while_off = toggled.uploaded_units;
    for (unsigned frame = 0; frame < 5; ++frame) {
      toggled.GuestWrite(16 * kUnit, 8, uint8_t(100 + frame));
      assert(toggled.RequestRange(16 * kUnit, kUnit, true) && toggled.RequestRange(16 * kUnit, kUnit, true));
      assert(!std::memcmp(&toggled.gpu[16 * kUnit], &toggled.cpu[16 * kUnit], kUnit));
    }
    // Off: a fault per write, and one upload of the requested unit per write, not one per request.
    std::printf("Switched off: %llu faults and %llu units uploaded for 5 writes and 10 requests\n",
                (unsigned long long)(toggled.faults - faults_while_off), (unsigned long long)(toggled.uploaded_units - uploads_while_off));
    assert(toggled.faults == faults_while_off + 5 && toggled.uploaded_units == uploads_while_off + 5);
    gpu_diag::unwatched_pages = true;
    for (unsigned frame = 0; frame < 6; ++frame) {
      toggled.GuestWrite(16 * kUnit, 8, uint8_t(200 + frame));
      assert(toggled.RequestRange(16 * kUnit, kUnit, true));
      assert(!std::memcmp(&toggled.gpu[16 * kUnit], &toggled.cpu[16 * kUnit], kUnit));
    }
    assert(unwatched16());
  }
  // The same random history with the feature off and on.
  uint64_t faults_off = 0, faults_on = 0, peak_unwatched = 0, compared = 0, switches = 0;
  for (unsigned seed = 1; seed <= 40; ++seed) {
    SharedMemory off, on;
    bool candidate = true;  // The Guide switch, flipped a few times in each history.
    std::mt19937 rng(seed * 7919);
    // A few places the guest keeps coming back to, so that units do heat up.
    uint32_t favourites[6];
    for (auto& f : favourites) f = rng() % (kUnits - 8) * kUnit;
    for (unsigned step = 0; step < 6000; ++step) {
      const unsigned op = rng() % 100;
      const bool favourite = rng() % 3 != 0;
      uint32_t address = favourite ? favourites[rng() % 6] + rng() % (3 * kUnit) : rng() % (SharedMemory::kBufferSize - 5 * kUnit);
      uint32_t length = 1 + rng() % (3 * kUnit);
      const uint8_t value = uint8_t(rng());
      const bool allow = rng() % 4 != 0;
      if (op < 45) {
        gpu_diag::unwatched_pages = false; off.GuestWrite(address, length, value);
        gpu_diag::unwatched_pages = candidate; on.GuestWrite(address, length, value);
      } else if (op < 90) {
        gpu_diag::unwatched_pages = false; const bool a = off.RequestRange(address, length, allow);
        gpu_diag::unwatched_pages = candidate; const bool b = on.RequestRange(address, length, allow);
        assert(a && b);
        const uint32_t first = address & ~(kUnit - 1), bytes = ((address + length - 1) | (kUnit - 1)) + 1 - first;
        if (std::memcmp(&off.gpu[first], &on.gpu[first], bytes)) {
          std::printf("seed %u step %u: GPU copy differs for request %08X+%X allow %d\n", seed, step, address, length, int(allow));
          return 1;
        }
        compared += bytes;
      } else if (op < 93) {
        gpu_diag::unwatched_pages = false; off.GpuWrite(address, length, value);
        gpu_diag::unwatched_pages = candidate; on.GpuWrite(address, length, value);
      } else if (op < 96) {
        gpu_diag::unwatched_pages = false; off.Release(address, length);
        gpu_diag::unwatched_pages = candidate; on.Release(address, length);
      } else if (op < 98) {
        // A watch is registered (a texture was loaded from these units).
        gpu_diag::unwatched_pages = false; off.RequestRange(address, length); off.PinWatched(address >> 12, (address + length - 1) >> 12);
        gpu_diag::unwatched_pages = candidate; on.RequestRange(address, length); on.PinWatched(address >> 12, (address + length - 1) >> 12);
      } else if (op < 99) {
        gpu_diag::unwatched_pages = candidate; on.AgeUnwatchedPages();
      } else if (rng() % 8 == 0) {
        candidate = !candidate; ++switches;
      }
      peak_unwatched = std::max<uint64_t>(peak_unwatched, on.Unwatched());
      // What is valid is watched, in both: a valid unit's host page is armed unless the GPU wrote it... or it was never writable; here every unit is writable.
      for (uint32_t unit = 0; unit < kUnits; ++unit) {
        const bool valid_on = on.valid_words[unit / 64] >> (unit % 64) & 1;
        if (valid_on) assert(on.armed[unit / kHostUnits]);
        if (on.unwatched_bits_[unit / 64] >> (unit % 64) & 1) assert(!valid_on && !(on.pinned_bits_[unit / 64] >> (unit % 64) & 1));
      }
    }
    faults_off += off.faults; faults_on += on.faults;
  }
  std::printf("40 histories of 6000 steps: %llu bytes of requested ranges equal; faults %llu off, %llu on; up to %llu units unwatched; switched %llu times\n",
              (unsigned long long)compared, (unsigned long long)faults_off, (unsigned long long)faults_on, (unsigned long long)peak_unwatched,
              (unsigned long long)switches);
  assert(peak_unwatched > 0 && faults_on < faults_off && switches > 100);
  std::puts("PASS: the GPU copy of every requested range is the same with unwatched pages on and off");
}
'''
(out / 'check.cpp').write_text(harness)
subprocess.run(['clang++-18', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined', '-I' + str(root / 'include'),
                str(out / 'check.cpp'), '-o', str(out / 'check')], check=True)
subprocess.run([str(out / 'check')], check=True)
