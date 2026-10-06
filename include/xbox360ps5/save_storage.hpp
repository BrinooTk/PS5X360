// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <string>
#include <chrono>
#include <fstream>
namespace xbox360ps5 {
struct SaveStorage { std::filesystem::path root; std::string notice; };
// The entire content tree includes profiles, achievements and saved packages.
// Publish an initial copy atomically; never merge an old snapshot into live saves.
inline SaveStorage PrepareSaveStorage(const std::filesystem::path& legacy,
                                     const std::filesystem::path& destination) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const auto fallback = [&](const std::string& reason) {
    return SaveStorage{legacy, "Homebrew saves unavailable: " + reason + "; using " + legacy.string()};
  };
  fs::create_directories(destination.parent_path(), ec);
  if (ec) return fallback(ec.message());
  const auto stage = destination.parent_path() / (".saves-check-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!fs::create_directory(stage, ec) || ec) return fallback("cannot create staging directory");
  // Creating a directory alone does not establish that file writes work.
  bool writable;
  { std::ofstream probe(stage / "probe", std::ios::binary); probe << "save"; probe.flush(); writable = probe.good(); }
  fs::remove(stage / "probe", ec);
  if (!writable || ec) { fs::remove_all(stage, ec); return fallback("write probe failed"); }
  const bool exists = fs::exists(destination, ec);
  if (ec || (exists && !fs::is_directory(destination, ec))) {
    fs::remove_all(stage, ec); return fallback("invalid save directory");
  }
  if (exists) {
    // Probe the actual directory too: a writable parent is not enough.
    const auto probe_dir = destination / stage.filename();
    bool ok = fs::create_directory(probe_dir, ec);
    if (ok && !ec) {
      { std::ofstream probe(probe_dir / "probe"); probe << "save"; probe.flush(); ok = probe.good(); }
      fs::remove_all(probe_dir, ec);
    }
    fs::remove_all(stage, ec);
    return ok ? SaveStorage{destination, "Homebrew save storage ready"} : fallback("save directory is not writable");
  }
  const bool old_exists = fs::exists(legacy, ec);
  if (!ec && old_exists) {
    // Do not follow links from user content into unrelated filesystem trees.
    for (fs::recursive_directory_iterator it(legacy, ec), end; !ec && it != end; it.increment(ec)) {
      if (it->is_symlink(ec)) { ec = std::make_error_code(std::errc::too_many_symbolic_link_levels); break; }
    }
    if (!ec) fs::copy(legacy, stage, fs::copy_options::recursive, ec);
  }
  if (ec) {
    const auto reason = ec.message(); fs::remove_all(stage, ec); return fallback(reason);
  }
  fs::rename(stage, destination, ec);
  if (ec) { const auto reason = ec.message(); fs::remove_all(stage, ec); return fallback(reason); }
  return {destination, old_exists ? "Saves copied to homebrew; original content retained" : "Homebrew save storage ready"};
}
}
