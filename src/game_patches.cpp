// SPDX-License-Identifier: MIT
#include "xbox360ps5/game_patches.hpp"
#include "xbox360ps5/patch_runtime.hpp"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/cvar.h"
DECLARE_bool(vsync);
#include "xenia/cpu/xex_module.h"
#include "xenia/kernel/user_module.h"
#include "xenia/memory.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#define XXH_INLINE_ALL
#include "third_party/xxhash/xxhash.h"
namespace xbox360ps5 {
namespace {
namespace fs = std::filesystem;
uint64_t last_hash = 0;
int last_applied = 0;

std::vector<fs::path> PatchFolders() {
#if XE_PLATFORM_PS5
  return {"/app0/assets/patches"};
#else
  std::vector<fs::path> folders{"assets/patches", "patches"};
  if (const char* folder = std::getenv("XBOX360PS5_PATCHES")) folders.insert(folders.begin(), folder);
  return folders;
#endif
}
fs::path ChoiceFile(uint32_t title_id) {
  char name[32];
  std::snprintf(name, sizeof(name), "%08X.txt", title_id);
#if XE_PLATFORM_PS5
  return fs::path("/download0/xbox360ps5/patches") / name;
#else
  if (const char* folder = std::getenv("XBOX360PS5_PATCH_CHOICES")) return fs::path(folder) / name;
  return fs::temp_directory_path() / "xbox360ps5-patches" / name;
#endif
}
std::string Trim(const std::string& text) {
  const size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
// One value of the subset of TOML the patch files use: a quoted string, a
// number, a boolean. `rest` is advanced past the value.
bool TakeString(std::string& rest, std::string& value) {
  rest = Trim(rest);
  if (rest.empty() || (rest[0] != '"' && rest[0] != '\'')) return false;
  const char quote = rest[0];
  value.clear();
  size_t at = 1;
  for (; at < rest.size() && rest[at] != quote; ++at) {
    if (quote == '"' && rest[at] == '\\' && at + 1 < rest.size()) {
      const char escaped = rest[++at];
      value += escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped;
    } else value += rest[at];
  }
  if (at >= rest.size()) return false;
  rest = rest.substr(at + 1);
  return true;
}
// A line without its comment; '#' inside a string is kept.
std::string StripComment(const std::string& line) {
  char quote = 0;
  for (size_t at = 0; at < line.size(); ++at) {
    const char c = line[at];
    if (quote) { if (c == '\\' && quote == '"') ++at; else if (c == quote) quote = 0; }
    else if (c == '"' || c == '\'') quote = c;
    else if (c == '#') return line.substr(0, at);
  }
  return line;
}
bool HexBytes(const std::string& text, std::vector<uint8_t>& bytes) {
  std::string digits;
  for (char c : text) if (!std::isspace(static_cast<unsigned char>(c))) digits += c;
  if (digits.rfind("0x", 0) == 0 || digits.rfind("0X", 0) == 0) digits.erase(0, 2);
  if (digits.empty() || digits.size() % 2) return false;
  for (size_t at = 0; at < digits.size(); at += 2) {
    char* end = nullptr;
    const std::string pair = digits.substr(at, 2);
    bytes.push_back(uint8_t(std::strtoul(pair.c_str(), &end, 16)));
    if (*end) return false;
  }
  return true;
}
// The bytes a `[[patch.<type>]]` entry writes, in the console's byte order.
bool Encode(const std::string& type, const std::string& raw, std::vector<uint8_t>& bytes) {
  std::string rest = raw, text;
  const auto big_endian = [&bytes](uint64_t value, int size) {
    for (int n = size - 1; n >= 0; --n) bytes.push_back(uint8_t(value >> (n * 8)));
  };
  if (type == "string") {
    if (!TakeString(rest, text)) return false;
    bytes.assign(text.begin(), text.end());
    return true;
  }
  if (type == "u16string") {
    if (!TakeString(rest, text)) return false;
    // UTF-8 to UTF-16BE; the patch files use the basic plane.
    for (size_t at = 0; at < text.size();) {
      unsigned code = static_cast<unsigned char>(text[at++]);
      if (code >= 0xE0 && at + 1 < text.size()) { code = (code & 0x0F) << 12 | (text[at] & 0x3F) << 6 | (text[at + 1] & 0x3F); at += 2; }
      else if (code >= 0xC0 && at < text.size()) { code = (code & 0x1F) << 6 | (text[at] & 0x3F); at += 1; }
      big_endian(code, 2);
    }
    return true;
  }
  if (type == "array") return TakeString(rest, text) && HexBytes(text, bytes);
  const std::string number = Trim(rest);
  if (number.empty()) return false;
  if (type == "f32" || type == "f64") {
    char* end = nullptr;
    const double value = std::strtod(number.c_str(), &end);
    if (end == number.c_str()) return false;
    if (type == "f32") { const float single = float(value); uint32_t bits; std::memcpy(&bits, &single, 4); big_endian(bits, 4); }
    else { uint64_t bits; std::memcpy(&bits, &value, 8); big_endian(bits, 8); }
    return true;
  }
  const int size = type == "be8" ? 1 : type == "be16" ? 2 : type == "be32" ? 4 : type == "be64" ? 8 : 0;
  if (!size) return false;
  char* end = nullptr;
  const uint64_t value = std::strtoull(number.c_str(), &end, 0);
  if (end == number.c_str()) return false;
  big_endian(value, size);
  return true;
}
}

bool ReadPatchFile(const std::string& path, PatchFile& file) {
  std::ifstream input(path);
  if (!input) return false;
  file = {};
  file.file = path;
  std::string section, type, line;
  uint32_t address = 0;
  bool have_address = false, have_value = false;
  std::string pending_value;
  const auto finish_write = [&] {
    // An entry is complete once both its address and value were read.
    if (!type.empty() && have_address && have_value && !file.patches.empty()) {
      PatchWrite write{address, {}};
      if (Encode(type, pending_value, write.bytes) && !write.bytes.empty())
        file.patches.back().writes.push_back(std::move(write));
      else XELOGW("Patches: unreadable {} value in {}", type, path);
    }
    have_address = have_value = false;
  };
  while (std::getline(input, line)) {
    line = Trim(StripComment(line));
    if (line.empty()) continue;
    if (line.rfind("[[", 0) == 0) {
      finish_write();
      section = Trim(line.substr(2, line.find("]]") == std::string::npos ? std::string::npos : line.find("]]") - 2));
      type.clear();
      if (section == "patch") file.patches.emplace_back();
      else if (section.rfind("patch.", 0) == 0) type = section.substr(6);
      continue;
    }
    const size_t split = line.find('=');
    if (split == std::string::npos) continue;
    const std::string key = Trim(line.substr(0, split));
    std::string value = Trim(line.substr(split + 1));
    // An array that continues on the following lines.
    while (!value.empty() && value[0] == '[' && value.find(']') == std::string::npos && std::getline(input, line))
      value += " " + Trim(StripComment(line));
    std::string text;
    if (section.empty()) {
      if (key == "title_name" && TakeString(value, text)) file.title_name = text;
      else if (key == "title_id" && TakeString(value, text)) file.title_id = uint32_t(std::strtoul(text.c_str(), nullptr, 16));
      else if (key == "hash") {
        std::string rest = value;
        if (!rest.empty() && rest[0] == '[') rest = rest.substr(1);
        while (TakeString(rest, text)) {
          if (!text.empty()) file.hashes.push_back(std::strtoull(text.c_str(), nullptr, 16));
          rest = Trim(rest);
          if (!rest.empty() && rest[0] == ',') rest = rest.substr(1);
        }
      }
    } else if (section == "patch" && !file.patches.empty()) {
      GamePatch& patch = file.patches.back();
      if (key == "name" && TakeString(value, text)) patch.name = text;
      else if (key == "desc" && TakeString(value, text)) patch.description = text;
      else if (key == "author" && TakeString(value, text)) patch.author = text;
      else if (key == "is_enabled") patch.enabled = value == "true";
    } else if (!type.empty()) {
      if (key == "address") {
        if (have_address) finish_write();
        address = uint32_t(std::strtoull(value.c_str(), nullptr, 0));
        have_address = true;
      } else if (key == "value") { pending_value = value; have_value = true; }
      if (have_address && have_value) finish_write();
    }
  }
  finish_write();
  return file.title_id != 0 && !file.patches.empty();
}

std::vector<PatchFile> LoadPatchFiles(uint32_t title_id) {
  std::vector<PatchFile> files;
  char prefix[12];
  std::snprintf(prefix, sizeof(prefix), "%08X", title_id);
  for (const auto& folder : PatchFolders()) {
    std::error_code error;
    for (fs::directory_iterator at(folder, error), end; !error && at != end; at.increment(error)) {
      const std::string name = at->path().filename().string();
      if (name.size() < 19 || name.compare(name.size() - 11, 11, ".patch.toml")) continue;
      std::string start = name.substr(0, 8);
      std::transform(start.begin(), start.end(), start.begin(), [](unsigned char c) { return char(std::toupper(c)); });
      if (start != prefix) continue;
      PatchFile file;
      if (ReadPatchFile(at->path().string(), file) && file.title_id == title_id) files.push_back(std::move(file));
    }
  }
  // The user's choices: one "1|name" or "0|name" per line.
  std::ifstream choices(ChoiceFile(title_id));
  for (std::string line; std::getline(choices, line);) {
    line = Trim(line);
    if (line.size() < 3 || line[1] != '|') continue;
    for (auto& file : files) for (auto& patch : file.patches)
      if (patch.name == line.substr(2)) patch.enabled = line[0] == '1';
  }
  return files;
}

void SavePatchChoice(uint32_t title_id, const std::string& name, bool enabled) {
  const fs::path path = ChoiceFile(title_id);
  std::error_code error;
  fs::create_directories(path.parent_path(), error);
  std::vector<std::string> lines;
  {
    std::ifstream input(path);
    for (std::string line; std::getline(input, line);)
      if (line.size() >= 3 && line.substr(2) != name) lines.push_back(line);
  }
  lines.push_back(std::string(enabled ? "1|" : "0|") + name);
  std::ofstream output(path, std::ios::trunc);
  for (const auto& line : lines) output << line << "\n";
}

namespace {
std::string PatchNameKey(std::string name) {
  name = Trim(name);
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return name;
}
bool SameWrites(const GamePatch& a, const GamePatch& b) {
  if (a.writes.size() != b.writes.size() || a.writes.empty()) return false;
  for (size_t n = 0; n < a.writes.size(); ++n)
    if (a.writes[n].address != b.writes[n].address || a.writes[n].bytes != b.writes[n].bytes) return false;
  return true;
}
}
bool PatchRequiresVsyncOff(const GamePatch& patch) {
  const auto desc = PatchNameKey(patch.description);
  return desc.find("disable v-sync") != std::string::npos || desc.find("disable vsync") != std::string::npos ||
         desc.find("vsync must be disabled") != std::string::npos;
}
bool PatchWritesConflict(const GamePatch& a, const GamePatch& b) {
  for (const auto& x : a.writes) for (const auto& y : b.writes) {
    const uint64_t begin = std::max<uint64_t>(x.address, y.address);
    const uint64_t end = std::min(uint64_t(x.address) + x.bytes.size(), uint64_t(y.address) + y.bytes.size());
    for (uint64_t at = begin; at < end; ++at)
      if (x.bytes[size_t(at - x.address)] != y.bytes[size_t(at - y.address)]) return true;
  }
  return false;
}
std::vector<PatchFile> SelectPatchFiles(std::vector<PatchFile> files, uint64_t hash) {
  std::stable_sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.file < b.file; });
  std::vector<PatchFile> result;
  std::vector<GamePatch> seen;
  for (auto& file : files) {
    if (hash && std::find(file.hashes.begin(), file.hashes.end(), hash) == file.hashes.end()) continue;
    std::vector<GamePatch> unique;
    for (auto& patch : file.patches) {
      bool duplicate = false;
      for (const auto& previous : seen)
        if (PatchNameKey(previous.name) == PatchNameKey(patch.name) || SameWrites(previous, patch)) { duplicate = true; break; }
      if (duplicate) continue;
      seen.push_back(patch);
      unique.push_back(std::move(patch));
    }
    file.patches = std::move(unique);
    if (!file.patches.empty()) result.push_back(std::move(file));
  }
  return result;
}

uint64_t ModuleHash(xe::Memory* memory, xe::kernel::UserModule* module) {
#if defined(XBOX360PS5_CANARY)
  // Canary calculated this before any patch writes or JIT precompilation.
  return module->hash().value_or(0);
#else
  const auto* xex = module->xex_module();
  const xe::BaseHeap* heap = memory->LookupHeap(xex->base_address());
  if (!heap) return 0;
  const uint32_t page_size = heap->page_size();
  const auto* security = xex->xex_security_info();
  // From the first code page to the last one, as Canary's patch hashes are made.
  uint32_t first = UINT32_MAX, last = UINT32_MAX;
  for (uint32_t n = 0; n < security->page_descriptor_count; ++n) {
    xe::xex2_page_descriptor descriptor;
    descriptor.value = xe::byte_swap(security->page_descriptors[n].value);
    if (descriptor.info != xe::XEX_SECTION_CODE) continue;
    if (first == UINT32_MAX) first = n;
    last = n;
  }
  if (first == UINT32_MAX) return 0;
  const uint32_t start = xex->base_address() + first * page_size;
  const uint32_t end = xex->base_address() + (last + 1) * page_size;
  return XXH3_64bits(memory->TranslateVirtual(start), end - start);
#endif
}

void ApplyGamePatches(xe::Memory* memory, xe::kernel::UserModule* module, uint32_t title_id) {
  if (!module->is_dll_module()) patch_vsync_off.store(false, std::memory_order_relaxed);
  last_hash = ModuleHash(memory, module);
  last_applied = 0;
  XELOGW("Patches: title {:08X}, executable hash {:016X}", title_id, last_hash);
  auto files = LoadPatchFiles(title_id);
  for (const auto& file : files) if (std::find(file.hashes.begin(), file.hashes.end(), last_hash) == file.hashes.end())
    XELOGW("Patches: {} is for another version of this game (hash not listed)", file.file);
  std::vector<GamePatch> accepted;
  for (const PatchFile& file : SelectPatchFiles(std::move(files), last_hash)) {
    if (std::find(file.hashes.begin(), file.hashes.end(), last_hash) == file.hashes.end()) {
      XELOGW("Patches: {} is for another version of this game (hash not listed)", file.file);
      continue;
    }
    for (const GamePatch& patch : file.patches) {
      if (!patch.enabled) continue;
      bool conflict = false;
      for (const auto& previous : accepted) if (PatchWritesConflict(previous, patch)) {
        XELOGE("Patches: skipped {} because its writes conflict with {}", patch.name, previous.name);
        conflict = true;
        break;
      }
      if (conflict) continue;
      struct Pending {
        const PatchWrite* write;
        xe::BaseHeap* heap;
        uint32_t protection;
      };
      std::vector<Pending> pending;
      bool applied = !patch.writes.empty();
      // Validate every range and make every page writable before changing bytes.
      // An invalid multi-write patch must not leave a half-patched executable.
      for (const PatchWrite& write : patch.writes) {
        xe::BaseHeap* heap = memory->LookupHeap(write.address);
        uint32_t protection = 0;
        const uint64_t end = uint64_t(write.address) + write.bytes.size();
        xe::HeapAllocationInfo info;
        if (!heap || write.bytes.empty() || end > 0x100000000ull ||
            !heap->QueryRegionInfo(write.address, &info) ||
            !(info.state & xe::kMemoryAllocationCommit) ||
            end > uint64_t(info.base_address) + info.region_size ||
            !heap->QueryProtect(write.address, &protection)) {
          XELOGE("Patches: invalid/uncommitted write for {} at {:08X}", patch.name, write.address);
          applied = false;
          break;
        }
        pending.push_back({&write, heap, protection});
      }
      size_t writable = 0;
      if (applied) for (const auto& entry : pending) {
        if (!entry.heap->Protect(entry.write->address, uint32_t(entry.write->bytes.size()),
                                xe::kMemoryProtectRead | xe::kMemoryProtectWrite)) {
          applied = false;
          break;
        }
        ++writable;
      }
      if (applied) for (const auto& entry : pending) {
        const auto& write = *entry.write;
        std::memcpy(memory->TranslateVirtual(write.address), write.bytes.data(), write.bytes.size());
        applied &= !std::memcmp(memory->TranslateVirtual(write.address), write.bytes.data(), write.bytes.size());
      }
      for (size_t n = 0; n < writable; ++n) {
        const auto& entry = pending[n];
        applied &= entry.heap->Protect(entry.write->address, uint32_t(entry.write->bytes.size()), entry.protection);
      }
      if (!applied) {
        XELOGE("Patches: {} failed validation/write/protection restoration; not counted as applied", patch.name);
        continue;
      }
      std::string description = patch.description;
      std::transform(description.begin(), description.end(), description.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      if (EffectiveVsync(cvars::vsync) && !PatchRequiresVsyncOff(patch) && (description.find("v-sync") != std::string::npos || description.find("vsync") != std::string::npos))
        XELOGW("Patches: {} mentions VSync in its requirements; currently ON. Check the patch description and restart after changing Settings", patch.name);
      if (PatchRequiresVsyncOff(patch)) {
        patch_vsync_off.store(true, std::memory_order_relaxed);
        XELOGW("Patches: {} requires VSync off; enabling the per-title override", patch.name);
      }
      accepted.push_back(patch);
      ++last_applied;
      XELOGW("Patches: applied \"{}\" ({} writes)", patch.name, patch.writes.size());
    }
  }
}
uint64_t LastModuleHash() { return last_hash; }
int LastAppliedPatches() { return last_applied; }
}

#if defined(XBOX360PS5_CANARY)
extern "C" void Xbox360PS5ApplyGamePatches(xe::Memory* memory, xe::kernel::UserModule* module, uint32_t title_id) {
  xbox360ps5::ApplyGamePatches(memory, module, title_id);
}
#endif
