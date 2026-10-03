// SPDX-License-Identifier: MIT
// Game patches in the Xenia Canary format (`<title id> - <name>.patch.toml`):
// values written into the game's memory after it is loaded and before it runs.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace xe {
class Memory;
namespace kernel { class UserModule; }
}
namespace xbox360ps5 {
struct PatchWrite { uint32_t address; std::vector<uint8_t> bytes; };
struct GamePatch {
  std::string name, description, author;
  bool enabled = false;  // The file's default, replaced by the user's choice.
  std::vector<PatchWrite> writes;
};
struct PatchFile {
  std::string file, title_name;
  uint32_t title_id = 0;
  std::vector<uint64_t> hashes;  // Executables the addresses are valid for.
  std::vector<GamePatch> patches;
};
// Reads one patch file. False when it is not a usable patch file.
bool ReadPatchFile(const std::string& path, PatchFile& file);
// The patch files for a title, with the user's on/off choices applied.
std::vector<PatchFile> LoadPatchFiles(uint32_t title_id);
void SavePatchChoice(uint32_t title_id, const std::string& name, bool enabled);
// Hash of the loaded executable's code, as Xenia Canary computes it.
uint64_t ModuleHash(xe::Memory* memory, xe::kernel::UserModule* module);
// Writes the enabled patches whose file lists this executable's hash.
void ApplyGamePatches(xe::Memory* memory, xe::kernel::UserModule* module, uint32_t title_id);
// The hash of the last executable ApplyGamePatches saw, and how many patches it applied.
uint64_t LastModuleHash();
int LastAppliedPatches();
}
