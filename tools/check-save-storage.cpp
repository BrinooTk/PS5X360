// SPDX-License-Identifier: MIT
#include "xbox360ps5/save_storage.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>
int main() {
  namespace fs = std::filesystem;
  const auto base = fs::temp_directory_path() / ("ps5x360-save-test-" + std::to_string(getpid()));
  const auto old = base / "content", target = base / "homebrew" / "saves";
  fs::create_directories(old / "0009000000000001" / "454108CF");
  const auto relative = fs::path("0009000000000001/454108CF/save.bin");
  { std::ofstream f(old / relative); f << "original"; }
  auto result = xbox360ps5::PrepareSaveStorage(old, target);
  assert(result.root == target && fs::exists(old / relative) && fs::exists(target / relative));
  { std::ofstream f(target / relative); f << "new progress"; }
  result = xbox360ps5::PrepareSaveStorage(old, target);
  std::string content; { std::ifstream f(target / relative); std::getline(f, content); }
  assert(content == "new progress"); // restarting must not restore stale saves
  const auto blocked = base / "blocked";
  { std::ofstream f(blocked); f << "not a directory"; }
  assert(xbox360ps5::PrepareSaveStorage(old, blocked / "saves").root == old);
  fs::create_symlink(old / relative, old / "external-link");
  assert(xbox360ps5::PrepareSaveStorage(old, base / "unsafe/saves").root == old);
  assert(!fs::exists(base / "unsafe/saves"));
  fs::remove_all(base);
  std::cout << "Save migration, original preservation, stale-copy protection and fallback passed\n";
}
