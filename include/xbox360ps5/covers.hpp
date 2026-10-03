// SPDX-License-Identifier: MIT
// Box art for the launcher, fetched from XboxUnity (the service the Aurora
// dashboard uses) over plain HTTP, and title ids read from a game's own files
// so that covers and patches are found before the game first runs.
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace xbox360ps5 {
// The title id in an executable's header (XEX2), "" when there is none.
std::string ReadXexTitleId(const std::filesystem::path& xex);
// The title id of the default.xex inside a disc image (XDVDFS), "" when none.
std::string ReadIsoTitleId(const std::filesystem::path& iso);
// Where a downloaded cover is kept: <folder>/<TITLEID>.jpg (or .png).
std::filesystem::path CoverFile(const std::string& title_id);
// Downloads, one after another on a thread of its own, the covers the list
// does not have yet.
class CoverDownloader {
 public:
  ~CoverDownloader();
  void Start(std::vector<std::string> title_ids);
  bool Busy() const { return busy_; }
  // True once after a run that downloaded covers, so the launcher reloads them.
  bool TakeFinished() { return finished_.exchange(false); }
  std::string Status() const;
 private:
  void Run(std::vector<std::string> title_ids);
  std::thread thread_;
  std::atomic<bool> busy_{false}, finished_{false};
  mutable std::mutex mutex_;
  std::string status_;
};
}
