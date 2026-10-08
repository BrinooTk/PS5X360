// SPDX-License-Identifier: MIT
// Launch arguments from a home screen forwarder, after ProsperoEden's
// (blackbearreloaded/ProsperoEden#13). A forwarder is a small app with its own
// home screen tile that starts PPSA50011 with
//
//   --rom <file>        the game: an absolute path, or one inside the library's
//                       game folders (Settings, game_paths.hpp); a game folder
//                       stands for its default.xex, as the library lists it
//   --exit-after-game   when that game goes back to the launcher, close PS5X360
//
// and src/canary_game_main.cpp starts that game without the launcher
// (docs/FORWARDER.md). Header only: tools/check-forward.cpp checks it on a PC.
#pragma once
#include <algorithm>
#include <cctype>
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

// The file the emulator starts for a path: the path itself when it is a file
// (an .xex, an .iso, a GOD/STFS package); for a folder, as the library lists an
// extracted game (Launcher::Scan), its default.xex or else its only .xex.
// Empty when there is none.
inline std::filesystem::path LaunchFile(const std::filesystem::path& path) {
  std::error_code error;
  if (std::filesystem::is_regular_file(path, error)) return path;
  if (!std::filesystem::is_directory(path, error)) return {};
  std::filesystem::path executable;
  int executables = 0;
  for (std::filesystem::directory_iterator at(path, error), end; !error && at != end; at.increment(error)) {
    std::string extension = at->path().extension().string(), name = at->path().filename().string();
    for (auto* text : {&extension, &name})
      std::transform(text->begin(), text->end(), text->begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::error_code kind_error;
    if (extension != ".xex" || at->is_directory(kind_error)) continue;
    ++executables;
    if (name == "default.xex") return at->path();
    executable = at->path();
  }
  return executables == 1 ? executable : std::filesystem::path();
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
    const auto file = LaunchFile(candidate);
    if (!file.empty()) return file;
    error = candidate.string() + ": no default.xex (or single .xex) in this folder";
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
