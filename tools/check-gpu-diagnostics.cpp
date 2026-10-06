#include "xbox360ps5/gpu_diagnostics.hpp"
#include "xbox360ps5/cache_io.hpp"
#include <cassert>
#include <fstream>
#include <thread>
#include <vector>

int main() {
  using namespace xbox360ps5;
  using namespace gpu_diag;
  Reset();
  assert(Call(Kind::submit, [](int n) { return n; }, 7) == 7);
  assert(counters[1].Take().calls == 0);
  {
    Scope disabled(Kind::cpu_draw);
  }
  assert(counters[static_cast<unsigned>(Kind::cpu_draw)].Take().calls == 0);
  stages_enabled = true;
  enabled = true;
  stages_enabled = false;
  { Scope inert(Kind::cpu_draw); }
  assert(counters[static_cast<unsigned>(Kind::cpu_draw)].Take().calls == 0);
  stages_enabled = true;
  auto early_return = [] {
    Scope outer(Kind::cpu_draw);
    Scope nested(Kind::cpu_upload);
    return 42;
  };
  assert(early_return() == 42);
  assert(counters[static_cast<unsigned>(Kind::cpu_draw)].Take().calls == 1);
  assert(counters[static_cast<unsigned>(Kind::cpu_upload)].Take().calls == 1);
  // A scope completes even if diagnostics are toggled while it is active.
  {
    Scope scope(Kind::cpu_completion);
    enabled = false;
  }
  assert(counters[static_cast<unsigned>(Kind::cpu_completion)].Take().calls == 1);
  stages_enabled = true;
  enabled = true;
  assert(Call(Kind::submit, [] { return -4; }) == -4);
  assert(counters[1].Take().errors == 1);
  // Hot-path instrumentation must time only one call in 1024.
  counters[static_cast<unsigned>(Kind::cpu_draw)].Take();
  for (unsigned i = 0; i < 100000; ++i) {
    Scope hot(Kind::cpu_draw);
  }
  const auto sampled = counters[static_cast<unsigned>(Kind::cpu_draw)].Take();
  std::printf("Hot-path timed calls: %llu / 100000\n", (unsigned long long)sampled.calls);
  assert(sampled.calls <= 98 && sampled.calls >= 97);
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) threads.emplace_back([] {
    for (int j = 0; j < 10000; ++j) counters[0].Add(100 + j % 10, false);
  });
  for (auto& t : threads) t.join();
  const auto reading = counters[0].Take();
  assert(reading.calls == 40000 && reading.worst == 109);
  assert(reading.nanoseconds == 4180000);
  assert(counters[0].Take().calls == 0);
  assert(ValidPipelineCacheSize(0));
  assert(!ValidPipelineCacheSize(-1));
  assert(!ValidPipelineCacheSize(kMaxPipelineCacheBytes + 1));
  const auto folder = std::filesystem::temp_directory_path() / "ps5x360-cache-io-check";
  std::filesystem::create_directories(folder);
  const auto cache = folder / "cache.bin";
  assert(WriteCacheAtomically(cache, "old", 3));
  assert(WriteCacheAtomically(cache, "new", 3));
  std::ifstream input(cache);
  std::string value; input >> value; input.close();
  assert(value == "new");
  // Force temporary-file creation to fail; the original must survive.
  const auto blocked = folder / "cache.bin.pending-2";
  std::filesystem::create_directory(blocked);
  assert(!WriteCacheAtomically(cache, "bad", 3));
  std::ifstream original(cache); original >> value; original.close();
  assert(value == "new");
  std::filesystem::remove(blocked);
  std::filesystem::remove(cache);
  std::filesystem::remove(folder);
  std::puts("PASS: counters, concurrent accounting, pass-through, cache limits and failed-write preservation");
}
