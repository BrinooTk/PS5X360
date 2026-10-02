// SPDX-License-Identifier: MIT
// Actual upstream VirtualFileSystem + HostPathDevice, no guest kernel stubs.
#include "xenia/vfs/virtual_file_system.h"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/base/filesystem.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <cstdlib>
using xe::X_STATUS;
namespace {
unsigned cases = 0, failures = 0;
void Check(bool result, const char* description) {
  ++cases; failures += !result;
  std::printf("%s %s\n",result ? "PASS" : "FAIL",description);
}
bool Mount(xe::vfs::VirtualFileSystem& fs, const std::filesystem::path& root, bool read_only) {
  auto device = std::make_unique<xe::vfs::HostPathDevice>("\\Device\\Cdrom0",root,read_only);
  if (!device->Initialize()) return false;
  return fs.RegisterDevice(std::move(device)) && fs.RegisterSymbolicLink("game:","\\Device\\Cdrom0") &&
         fs.RegisterSymbolicLink("d:","\\Device\\Cdrom0");
}
int InspectGame(const std::filesystem::path& root) {
  xe::vfs::VirtualFileSystem fs;
  if (!Mount(fs,root,true)) return 2;
  auto* entry = fs.ResolvePath("game:\\DEFAULT.XEX");
  if (!entry) return 3;
  xe::vfs::File* file = nullptr;
  if (entry->Open(xe::vfs::FileAccess::kGenericRead,&file) != X_STATUS_SUCCESS || !file) return 4;
  std::array<char,4> header{}; size_t read = 0;
  const auto status = file->ReadSync(header.data(),header.size(),0,&read);
  file->Destroy();
  if (status != X_STATUS_SUCCESS || read != 4 || header != std::array<char,4>{'X','E','X','2'}) return 5;
  std::printf("ACTUAL VFS GAME HEADER XEX2 BYTES %llu (read-only; not executed)\n",static_cast<unsigned long long>(entry->size()));
  return 0;
}
}
int main(int argc,char** argv) {
  if (argc == 3 && std::string_view(argv[1]) == "--game-root") return InspectGame(argv[2]);
  char directory[] = "vfs-fixture-XXXXXX";
  if (!mkdtemp(directory)) return 2;
  const auto root = std::filesystem::absolute(directory);
  std::filesystem::create_directories(root / "MixedCase");
  {
    std::ofstream fixture(root / "MixedCase/default.xex",std::ios::binary);
    fixture.write("XEX2abcd",8);
  }
  xe::vfs::VirtualFileSystem fs;
  Check(Mount(fs,root,true),"mount real readonly host device and guest aliases");
  auto* entry = fs.ResolvePath("GAME:\\mixedcase\\DEFAULT.XEX");
  Check(entry && entry->size() == 8,"guest resolution is case insensitive with correct size");
  if (!entry) return 1;
  Check(fs.ResolvePath("d:\\MixedCase\\default.xex") == entry,"disc and game aliases select same file");
  Check(fs.ResolvePath("game:\\missing.xex") == nullptr,"missing file is not synthesized");
  xe::vfs::File* file = nullptr;
  Check(entry->Open(xe::vfs::FileAccess::kGenericRead,&file) == X_STATUS_SUCCESS && file,"open actual host file through guest entry");
  if (!file) return 1;
  std::array<char,4> data{}; size_t count = 0;
  Check(file->ReadSync(data.data(),4,0,&count) == X_STATUS_SUCCESS && count == 4 && data == std::array<char,4>{'X','E','X','2'},"read XEX header through actual VFS");
  Check(file->ReadSync(data.data(),4,4,&count) == X_STATUS_SUCCESS && count == 4 && data == std::array<char,4>{'a','b','c','d'},"offset reads use correct position");
  Check(file->ReadSync(data.data(),4,8,&count) == X_STATUS_SUCCESS && count == 0,"EOF reports zero bytes");
  Check(file->WriteSync(data.data(),4,0,&count) == X_STATUS_ACCESS_DENIED,"readonly file refuses writes");
  file->Destroy(); file = nullptr;
  Check(entry->Open(xe::vfs::FileAccess::kGenericWrite,&file) == X_STATUS_ACCESS_DENIED && !file,"generic write cannot bypass readonly device");
  Check(!entry->OpenMapped(xe::MappedMemory::Mode::kReadWrite),"readonly device rejects writable mapping");
  xe::filesystem::FileInfo info{};
  Check(xe::filesystem::GetInfo(root / "MixedCase/default.xex",&info) && info.total_size == 8,"stat adapter initializes file length");
  auto rw = xe::filesystem::FileHandle::OpenExisting(root / "MixedCase/default.xex",xe::filesystem::FileAccess::kGenericRead | xe::filesystem::FileAccess::kGenericWrite);
  Check(rw && rw->Read(0,data.data(),4,&count) && rw->Write(4,"efgh",4,&count),"mixed read/write permissions become O_RDWR");
  count = 99;
  auto ro = xe::filesystem::FileHandle::OpenExisting(root / "MixedCase/default.xex",xe::filesystem::FileAccess::kGenericRead);
  Check(ro && !ro->Write(0,"zzzz",4,&count) && count == 0,"failed write count never wraps to SIZE_MAX");
  ro.reset(); rw.reset();
  Check(fs.UnregisterSymbolicLink("game:") && !fs.ResolvePath("game:\\MixedCase\\default.xex"),"unmount alias removes guest access");
  Check(fs.UnregisterSymbolicLink("d:") && fs.UnregisterDevice("\\Device\\Cdrom0"),"release real device after file handles close");
  std::filesystem::remove(root / "MixedCase/default.xex");
  std::filesystem::remove(root / "MixedCase");
  std::filesystem::remove(root);
  std::printf("ACTUAL VFS CASES %u FAILURES %u\n",cases,failures);
  return failures ? 1 : 0;
}
