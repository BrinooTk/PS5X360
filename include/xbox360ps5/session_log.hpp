// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <unistd.h>
namespace xbox360ps5 {
inline std::string LogFilenameName(const std::string& title) {
  std::string safe;
  for (size_t i = 0; i < title.size();) {
    const unsigned char c = title[i];
    const size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    if (i + length > title.size() || safe.size() + length > 96) break;
    if (c < 32 || std::string("<>:\"/\\|?*").find(char(c)) != std::string::npos) safe += '_';
    else safe.append(title, i, length);
    i += length;
  }
  while (!safe.empty() && (safe.back() == '.' || safe.back() == ' ')) safe.pop_back();
  return safe.empty() ? "Game" : safe;
}
// The writer thread and the UI thread share this destination. Keep the first
// segment (and crash reports) plus three rolling segments, at 8 MiB each.
class SessionLog {
 public:
  ~SessionLog() { if (file_) std::fclose(file_); }
  bool Begin(const std::filesystem::path& root, const std::string& title,
             const std::string& id, const std::string& game_path, const std::string& version) {
    std::lock_guard lock(mutex_);
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) return false;
    uint32_t hash = 2166136261u;
    for (unsigned char c : game_path) { hash ^= c; hash *= 16777619u; }
    char stamp[32], key[16];
    const auto now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S-UTC", &utc);
    std::snprintf(key, sizeof(key), "%08X", hash);
    const std::string prefix = "Game-" + LogFilenameName(title) + "-" + key + "-" + stamp;
    FILE* next = nullptr;
    std::filesystem::path path;
    for (unsigned serial = 0; serial < 1000; ++serial) {
      path = root / (prefix + "-" + std::to_string(serial) + ".log");
      const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_APPEND, 0644);
      if (fd < 0) {
        if (errno == EEXIST) continue;
        return false; // Permission/mount errors are not filename collisions.
      }
      next = fdopen(fd, "ab");
      if (!next) close(fd);
      break;
    }
    if (!next) return false;
    if (file_) std::fclose(file_);
    file_ = next;
    path_ = path;
    part_ = 0;
    created_.fill(false);
    metadata_ = "PS5X360 " + version + "\nGame: " + title + "\nTitle ID: " + id +
                "\nSource: " + game_path + "\nSession: " + stamp +
                "\nFirst segment preserved; .part1/.part2/.part3 rotate at 8 MiB.\n\n";
    Header();
    return true;
  }
  std::filesystem::path Path() const { std::lock_guard lock(mutex_); return path_; }
  void Write(const char* text, size_t size) {
    std::lock_guard lock(mutex_);
    if (!file_) return;
    if (bytes_ + size > 8u * 1024 * 1024) {
      const unsigned next_part = part_ % 3 + 1;
      const auto next_path = path_.parent_path() / (path_.stem().string() + ".part" + std::to_string(next_part) + ".log");
      const int fd = open(next_path.c_str(), O_WRONLY | O_CREAT | O_APPEND |
                          (created_[next_part] ? O_TRUNC : O_EXCL), 0644);
      FILE* next = fd >= 0 ? fdopen(fd, "ab") : nullptr;
      if (!next) { if (fd >= 0) close(fd); return; }
      std::fclose(file_); file_ = next; part_ = next_part; created_[part_] = true;
      Header();
    }
    bytes_ += std::fwrite(text, 1, size, file_);
  }
  void Flush() { std::lock_guard lock(mutex_); if (file_) std::fflush(file_); }
 private:
  void Header() { bytes_ = std::fwrite(metadata_.data(), 1, metadata_.size(), file_); std::fflush(file_); }
  mutable std::mutex mutex_;
  FILE* file_ = nullptr;
  std::filesystem::path path_;
  std::string metadata_;
  size_t bytes_ = 0;
  unsigned part_ = 0;
  std::array<bool, 4> created_{};
};
}
