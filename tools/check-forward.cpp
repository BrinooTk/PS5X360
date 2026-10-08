// SPDX-License-Identifier: MIT
// The forwarder's launch arguments (include/xbox360ps5/forward.hpp) on a PC.
//   clang++ -std=c++20 -Iinclude tools/check-forward.cpp -o check-forward && ./check-forward
#include "xbox360ps5/forward.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace xbox360ps5::forward;

template <size_t N>
static Args ParseOf(const char* (&argv)[N]) {
  return Parse(static_cast<int>(N), const_cast<char**>(argv));
}

static void Touch(const fs::path& path) {
  fs::create_directories(path.parent_path());
  std::ofstream(path) << "x";
}

int main() {
  {
    const char* argv[] = {"eboot.bin", "--rom", "Halo 3.iso", "--exit-after-game"};
    const Args a = ParseOf(argv);
    assert(a.rom == "Halo 3.iso" && a.exit_after_game);
  }
  {
    const char* argv[] = {"eboot.bin", "--rom=/mnt/usb0/xbox360/Halo 3/default.xex"};
    const Args a = ParseOf(argv);
    assert(a.rom == "/mnt/usb0/xbox360/Halo 3/default.xex" && !a.exit_after_game);
  }
  {
    const char* argv[] = {"eboot.bin", "/app0/assets/roms/game/default.xex"};  // the title's bare path: not --rom
    assert(ParseOf(argv).rom.empty());
    const char* dangling[] = {"eboot.bin", "--rom"};
    assert(ParseOf(dangling).rom.empty());
  }
  assert(Parse(0, nullptr).rom.empty());

  const std::vector<std::string> folders = {"/app0/assets/roms", "/data/xbox360"};
  assert((Candidates("Halo 3.iso", folders) ==
          std::vector<fs::path>{"/app0/assets/roms/Halo 3.iso", "/data/xbox360/Halo 3.iso"}));
  assert((Candidates("RPG/Fable 2/\r\n", folders) ==
          std::vector<fs::path>{"/app0/assets/roms/RPG/Fable 2", "/data/xbox360/RPG/Fable 2"}));
  assert((Candidates("/mnt/usb0/Halo.iso", folders) == std::vector<fs::path>{"/mnt/usb0/Halo.iso"}));
  assert(Candidates("../Halo.iso", folders).empty());
  assert(Candidates("RPG/../../x.iso", folders).empty());
  assert(Candidates("RPG/..x.iso", folders).size() == 2);
  assert(Candidates("", folders).empty());
  assert((Candidates("a.iso", {"/g", "/g", ""}) == std::vector<fs::path>{"/g/a.iso"}));

  // On disk: the second folder holds the games.
  const fs::path root = fs::temp_directory_path() / "ps5x360-check-forward";
  fs::remove_all(root);
  const std::vector<std::string> disk = {(root / "missing").string(), (root / "games").string()};
  Touch(root / "games/Halo 3.iso");
  Touch(root / "games/Fable 2/DEFAULT.XEX");
  Touch(root / "games/Fable 2/media.xex.bak");
  Touch(root / "games/Single/game.xex");
  Touch(root / "games/Two/a.xex");
  Touch(root / "games/Two/b.xex");
  fs::create_directories(root / "games/Empty");
  std::string error;
  assert(Find("Halo 3.iso", disk, error) == root / "games/Halo 3.iso");
  assert(Find("Fable 2", disk, error) == root / "games/Fable 2/DEFAULT.XEX");
  assert(Find("Single/", disk, error) == root / "games/Single/game.xex");
  assert(Find((root / "games/Fable 2").string(), disk, error) == root / "games/Fable 2/DEFAULT.XEX");
  assert(Find("Two", disk, error).empty() && error.find("no default.xex") != std::string::npos);
  assert(Find("Empty", disk, error).empty() && error.find("no default.xex") != std::string::npos);
  assert(Find("Gears.iso", disk, error).empty() && error == "Gears.iso: not found in the game folders");
  assert(Find("/nowhere/Gears.iso", disk, error).empty() && error == "/nowhere/Gears.iso: not found");
  assert(Find("../Gears.iso", disk, error).empty() && error.find("not a path inside") != std::string::npos);
  fs::remove_all(root);

  assert(OnceForwarded(OnceFlags(false)) && !OnceExitAfterGame(OnceFlags(false)));
  assert(OnceForwarded(OnceFlags(true)) && OnceExitAfterGame(OnceFlags(true)));
  assert(!OnceForwarded("") && !OnceExitAfterGame(""));

  std::puts("forwarder arguments: all checks passed");
  return 0;
}
