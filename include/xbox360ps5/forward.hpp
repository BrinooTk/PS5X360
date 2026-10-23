// SPDX-License-Identifier: MIT
// Launch arguments from a home screen forwarder, after ProsperoEden's
// (blackbearreloaded/ProsperoEden#13). A forwarder is a small app with its own
// home screen tile that starts PPSA50011 with
//
//   --rom <file>        the game: an absolute path, or one inside the library's
//                       game folders (Settings, game_paths.hpp); a game folder
//                       stands for its default.xex, or for the one game package
//                       (an XBLA or Games on Demand title) inside it, as the
//                       library lists them
//   --exit-after-game   when that game goes back to the launcher, close PS5X360
//
// and src/canary_game_main.cpp starts that game without the launcher
// (docs/FORWARDER.md). Header only: tools/check-forward.cpp checks it on a PC.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace xbox360ps5::forward {

struct Args {
  std::string rom;             // --rom <file> or --rom=<file>; empty when not given
  bool exit_after_game = false;  // --exit-after-game
};

// Unknown arguments are ignored. A first argument that is not an option is
// left to the caller (the title has always taken a bare path there).
inline Args Parse(int argc, char** argv) {
  Args result;
  if (argc <= 0 || argv == nullptr) return result;
  constexpr std::string_view rom_flag = "--rom", rom_prefix = "--rom=";
  for (int i = 0; i < argc; ++i) {
    if (argv[i] == nullptr) break;
    const std::string_view arg{argv[i]};
    if (arg == "--exit-after-game") {
      result.exit_after_game = true;
    } else if (arg == rom_flag) {
      if (i + 1 < argc && argv[i + 1] != nullptr) result.rom = argv[++i];
    } else if (arg.starts_with(rom_prefix)) {
      result.rom = std::string(arg.substr(rom_prefix.size()));
    }
  }
  return result;
}

// Where to look for the game: an absolute path as it is; a relative one inside
// each of the game folders, in their order, never above them (nothing for a
// path with a ".." part).
inline std::vector<std::filesystem::path> Candidates(std::string_view rom, const std::vector<std::string>& folders) {
  while (!rom.empty() && (rom.back() == ' ' || rom.back() == '\r' || rom.back() == '\n')) rom.remove_suffix(1);
  while (rom.size() > 1 && rom.back() == '/') rom.remove_suffix(1);  // a game folder given as "Halo 3/"
  if (rom.empty()) return {};
  if (rom.front() == '/') return {std::filesystem::path(rom)};
  for (std::string_view rest = rom; !rest.empty();) {
    const size_t slash = rest.find('/');
    if (rest.substr(0, slash) == "..") return {};
    if (slash == std::string_view::npos) break;
    rest.remove_prefix(slash + 1);
  }
  std::vector<std::filesystem::path> out;
  for (const auto& folder : folders) {
    if (folder.empty()) continue;
    const auto path = std::filesystem::path(folder) / std::string(rom);
    if (std::find(out.begin(), out.end(), path) == out.end()) out.push_back(path);
  }
  return out;
}

// An STFS/SVOD package of a kind the library lists as a game (Launcher::Scan,
// ReadPackage in src/launcher.cpp): a LIVE, PIRS or "CON " header whose content
// type is something to run - arcade (000D0000, the XBLA layout
// <title id>/000D0000/<package>), Games on Demand (00007000), an installed disc,
// a demo, a community game. Saves, DLC and title updates are not.
inline bool GamePackage(const std::filesystem::path& path) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return false;
  unsigned char header[0x348];
  const size_t got = std::fread(header, 1, sizeof(header), file);
  std::fclose(file);
  if (got < sizeof(header)) return false;
  if (std::memcmp(header, "LIVE", 4) && std::memcmp(header, "PIRS", 4) && std::memcmp(header, "CON ", 4)) return false;
  const uint32_t type = uint32_t(header[0x344]) << 24 | uint32_t(header[0x345]) << 16 | uint32_t(header[0x346]) << 8 | header[0x347];
  for (const uint32_t game : {0x000D0000u, 0x00080000u, 0x00007000u, 0x00004000u, 0x00005000u, 0x02000000u, 0x00060000u, 0x000C0000u})
    if (type == game) return true;
  return false;
}

inline std::string LowerCase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

// The file the emulator starts for a path: the path itself when it is a file
// (an .xex, an .iso, a GOD/STFS package); for a folder, as the library lists it
// (Launcher::Scan): an extracted game's default.xex or else its only .xex, or
// else the one game package below it (an XBLA title's Limbo/584109D1/000D0000/
// <package>, a Games on Demand title's 00007000 package), looked for as the
// library does - six levels down, not in a package's .data folder. Empty when
// there is none, or when the folder holds several games; why says which.
inline std::filesystem::path LaunchFile(const std::filesystem::path& path, std::string* why = nullptr) {
  std::error_code error;
  if (std::filesystem::is_regular_file(path, error)) return path;
  if (!std::filesystem::is_directory(path, error)) return {};
  std::filesystem::path executable;
  int executables = 0;
  for (std::filesystem::directory_iterator at(path, error), end; !error && at != end; at.increment(error)) {
    std::error_code kind_error;
    if (LowerCase(at->path().extension().string()) != ".xex" || at->is_directory(kind_error)) continue;
    ++executables;
    if (LowerCase(at->path().filename().string()) == "default.xex") return at->path();
    executable = at->path();
  }
  if (executables == 1) return executable;
  std::vector<std::filesystem::path> packages;
  const auto walk = [&packages](const auto& self, const std::filesystem::path& folder, int depth) -> void {
    std::error_code walk_error;
    std::vector<std::filesystem::path> folders;
    for (std::filesystem::directory_iterator at(folder, walk_error), end; !walk_error && at != end; at.increment(walk_error)) {
      std::error_code kind_error;
      const std::string extension = LowerCase(at->path().extension().string());
      if (at->is_directory(kind_error)) {
        if (depth < 6 && extension != ".data") folders.push_back(at->path());
      } else if ((extension.empty() || extension == ".xbla" || extension == ".god") && GamePackage(at->path())) {
        packages.push_back(at->path());
      }
    }
    std::sort(folders.begin(), folders.end());
    for (const auto& below : folders) self(self, below, depth + 1);
  };
  walk(walk, path, 0);
  if (executables == 0 && packages.size() == 1) return packages.front();
  if (why) {
    if (executables > 1)
      *why = "no default.xex and " + std::to_string(executables) + " .xex files in this folder; give the one to start";
    else if (packages.size() > 1)
      *why = std::to_string(packages.size()) + " game packages below this folder (" + packages[0].string() + ", " +
             packages[1].string() + (packages.size() > 2 ? ", ..." : "") + "); give the one to start";
    else
      *why = "no default.xex, single .xex or game package (XBLA, Games on Demand) in this folder";
  }
  return {};
}

// The first candidate that exists, as LaunchFile has it; error says why there
// is none.
inline std::filesystem::path Find(std::string_view rom, const std::vector<std::string>& folders, std::string& error) {
  const auto candidates = Candidates(rom, folders);
  if (candidates.empty()) {
    error = std::string(rom) + ": not a path inside the game folders (no \"..\")";
    return {};
  }
  for (const auto& candidate : candidates) {
    std::error_code exists_error;
    if (!std::filesystem::exists(candidate, exists_error)) continue;
    std::string why;
    const auto file = LaunchFile(candidate, &why);
    if (!file.empty()) return file;
    error = candidate.string() + ": " + why;
    return {};
  }
  error = std::string(rom) + ": not found" + (candidates.size() > 1 || candidates[0].string() != rom ? " in the game folders" : "");
  return {};
}

// launch-once.txt's third line: a forwarded game that restarts into itself
// (its own start options) stays forwarded.
inline std::string OnceFlags(bool exit_after_game) { return exit_after_game ? "forwarded exit-after-game" : "forwarded"; }
inline bool OnceForwarded(std::string_view flags) { return flags.starts_with("forwarded"); }
inline bool OnceExitAfterGame(std::string_view flags) { return flags.find("exit-after-game") != std::string_view::npos; }

}  // namespace xbox360ps5::forward
