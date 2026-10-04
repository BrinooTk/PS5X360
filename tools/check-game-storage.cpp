// SPDX-License-Identifier: MIT
#include "xbox360ps5/game_paths.hpp"
#include "xbox360ps5/session_log.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <thread>
namespace fs = std::filesystem;
std::string Read(const fs::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int main() {
  char pattern[] = "/tmp/ps5x360-storage-XXXXXX";
  const char* created = mkdtemp(pattern);
  assert(created);
  const fs::path root(created);
  assert(xbox360ps5::NormalizeGamePath(" /mnt/usb0/Games/../Games/\r ") == "/mnt/usb0/Games");
  assert(xbox360ps5::NormalizeGamePath("relative/games").empty());
  assert(xbox360ps5::NormalizeGamePath("/").empty());
  const auto config = root / "game_paths.txt";
  std::ofstream(config) << "/mnt/offline/Games\r\n/mnt/offline/Games/\ninvalid\n/app0/assets/roms\n";
  std::vector<std::string> paths;
  assert(xbox360ps5::ReadGamePaths(config, paths));
  assert(paths.size() == 2 && paths.front() == "/mnt/offline/Games");
  assert(xbox360ps5::WriteGamePaths(config, {"/mnt/new/Games"}));
  assert(xbox360ps5::ReadGamePaths(config, paths) && paths.size() == 1 && paths[0] == "/mnt/new/Games");
  assert(xbox360ps5::WriteGamePaths(config, {}));
  assert(xbox360ps5::ReadGamePaths(config, paths) && paths.empty());
  assert(xbox360ps5::LogFilenameName("A/B:C?D. ") == "A_B_C_D");
  fs::path first, second;
  {
    xbox360ps5::SessionLog log;
    assert(log.Begin(root / "LOGS", "Sonic / Test", "12345678", "/games/sonic/default.xex", "test-version"));
    first = log.Path();
    log.Write("FIRST GAME\n", 11); log.Flush();
    assert(Read(first).find("test-version") != std::string::npos);
    assert(Read(first).find("FIRST GAME") != std::string::npos);
    assert(log.Begin(root / "LOGS", "Sonic / Test", "12345678", "/games/sonic/default.xex", "test-version"));
    second = log.Path(); assert(second != first);
    std::thread a([&] { for (int i=0;i<100;++i) log.Write("thread-A\n",9); });
    std::thread b([&] { for (int i=0;i<100;++i) log.Write("thread-B\n",9); });
    a.join(); b.join(); log.Flush();
    assert(Read(second).find("FIRST GAME") == std::string::npos);
    const auto snapshot = Read(first);
    std::string block(1024*1024, 'x');
    for (int i=0;i<35;++i) log.Write(block.data(), block.size());
    log.Write("LATEST MARKER\n",14); log.Flush();
    assert(Read(first) == snapshot);
    unsigned segments = 0; bool latest = false;
    for (const auto& entry : fs::directory_iterator(root / "LOGS")) {
      if (entry.path().stem().string().find(second.stem().string()) != 0) continue;
      ++segments;
      assert(entry.file_size() <= 8u*1024*1024);
      const auto content = Read(entry.path());
      assert(content.find("Title ID: 12345678") != std::string::npos);
      latest |= content.find("LATEST MARKER") != std::string::npos;
    }
    assert(segments == 4 && latest);
    std::ofstream(root / "not-a-directory").put(0);
    assert(!log.Begin(root / "not-a-directory", "Failure", "", "", "test"));
    assert(log.Path() == second);
  }
  fs::remove_all(root); // Only this process's mkdtemp fixture.
  puts("PASS: path persistence, offline roots, normalization, per-session isolation, concurrent writes, bounded rotation and failure fallback");
}
