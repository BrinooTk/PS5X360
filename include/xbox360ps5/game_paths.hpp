// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
namespace xbox360ps5 {
inline std::string NormalizeGamePath(std::string path) {
  const auto first = path.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  path = path.substr(first, path.find_last_not_of(" \t\r\n") - first + 1);
  if (path.size() > 1024 || path.front() != '/' || path.find_first_of("\\\r\n") != std::string::npos ||
      path.find('\0') != std::string::npos) return {};
  path = std::filesystem::path(path).lexically_normal().generic_string();
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  return path == "/" ? std::string{} : path;
}
inline std::vector<std::string> DefaultGamePaths() {
  return {"/app0/assets/roms", "/data/xbox360", "/mnt/usb0/xbox360", "/mnt/usb1/xbox360",
          "/mnt/ext0/xbox360", "/mnt/ext1/xbox360"};
}
inline bool ReadGamePaths(const std::filesystem::path& file, std::vector<std::string>& paths) {
  std::ifstream input(file);
  if (!input) return false;
  std::vector<std::string> loaded;
  std::set<std::string> seen;
  for (std::string line; std::getline(input, line);) {
    const auto path = NormalizeGamePath(line);
    if (!path.empty() && seen.insert(path).second) loaded.push_back(path);
  }
  paths = std::move(loaded);
  return true;
}
inline bool WriteGamePaths(const std::filesystem::path& file, const std::vector<std::string>& paths) {
  const std::filesystem::path temporary = file.string() + ".tmp";
  std::ofstream output(temporary, std::ios::trunc);
  if (!output) return false;
  for (const auto& path : paths) output << path << '\n';
  output.close();
  if (!output) return false;
  std::error_code error;
  std::filesystem::rename(temporary, file, error);
  return !error;
}
}
