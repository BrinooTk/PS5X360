"""Exercise the core's real host-range commit with a fake heap: a watch on a 16 KiB host page must
survive the commit of a 4 KiB guest page in it."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
source = (root / ".deps/xenia-canary/src/xenia/memory.cc").read_text()
body = source[source.index("static memory::PageAccess WithinWatchLimit("):source.index("void RandomizeMemory(")]
out = root / "build/watch-limit-check"
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
namespace memory {
enum class PageAccess { kNoAccess = 0, kReadOnly = 1, kReadWrite = 3 };
enum class AllocationType { kReserve, kCommit };
size_t native = 0x4000;
size_t page_size() { return native; }
// What the host was last asked for, per 4 KiB, and how.
alignas(0x4000) uint8_t arena[64 * 0x1000];
PageAccess host[64];
unsigned commits = 0, protects = 0;
void Set(void* at, size_t bytes, PageAccess access) {
  const size_t first = (static_cast<uint8_t*>(at) - arena) / 0x1000;
  for (size_t i = 0; i < bytes / 0x1000; ++i) host[first + i] = access;
}
void* AllocFixed(void* at, size_t bytes, AllocationType, PageAccess access) { ++commits; Set(at, bytes, access); return at; }
bool Protect(void* at, size_t bytes, PageAccess access) {
  assert((static_cast<uint8_t*>(at) - arena) % native == 0 && bytes % native == 0);
  ++protects; Set(at, bytes, access); return true;
}
}
using Access = memory::PageAccess;
constexpr uint32_t kRead = 1, kWrite = 2;
Access ToPageAccess(uint32_t protect) { return protect & kWrite ? Access::kReadWrite : protect & kRead ? Access::kReadOnly : Access::kNoAccess; }
struct BaseHeap {
  uint32_t host_offset = 0;
  std::vector<uint32_t> guest = std::vector<uint32_t>(48, 0);  // Protection per 4 KiB guest page.
  std::vector<int> watch = std::vector<int>(16, 0);            // Per host page: 1 write watch, 2 read watch.
  template <typename T = uint8_t*> T TranslateRelative(size_t relative) const {
    return reinterpret_cast<T>(memory::arena + host_offset + relative);
  }
  uint32_t heap_size() const { return uint32_t(guest.size() * 0x1000); }
  uint32_t page_size() const { return 0x1000; }
  uint32_t heap_base() const { return 0; }
  bool QueryProtect(uint32_t address, uint32_t* out) { *out = guest[address >> 12]; return true; }
  Access HostPageWatchLimit(const void* host_page) const {
    const int kind = watch[(static_cast<const uint8_t*>(host_page) - memory::arena) / memory::native];
    return kind == 2 ? Access::kNoAccess : kind == 1 ? Access::kReadOnly : Access::kReadWrite;
  }
  // What the guest does: mark the pages and commit them through the core's function.
  void Commit(uint32_t offset, uint32_t size, uint32_t protect);
};
''' + body + r'''
void BaseHeap::Commit(uint32_t offset, uint32_t size, uint32_t protect) {
  for (uint32_t page = offset >> 12; page < (offset + size) >> 12; ++page) guest[page] = protect;
  assert(AllocateHostRange(*this, offset, size, memory::AllocationType::kCommit, protect));
}
Access HostOf(const BaseHeap& heap, uint32_t guest_page) { return memory::host[(heap.host_offset >> 12) + guest_page]; }
int main() {
  for (uint32_t host_offset : {0u, 0x1000u}) {  // 0x1000: the guest's 0xE0000000 heap on a 16 KiB host.
    for (int kind = 0; kind <= 2; ++kind) {
      memory::native = 0x4000;
      std::fill(std::begin(memory::host), std::end(memory::host), Access::kNoAccess);
      BaseHeap heap; heap.host_offset = host_offset;
      // A texture in guest pages 4..5, uploaded and watched: its host pages are closed.
      heap.Commit(0x4000, 0x2000, kRead | kWrite);
      assert(HostOf(heap, 4) == Access::kReadWrite && HostOf(heap, 5) == Access::kReadWrite);
      const Access closed = kind == 2 ? Access::kNoAccess : kind == 1 ? Access::kReadOnly : Access::kReadWrite;
      for (uint32_t page = 4; page <= 5; ++page) {
        const size_t host_page = (host_offset + page * 0x1000) / 0x4000;
        heap.watch[host_page] = kind;
        memory::Set(memory::arena + host_page * 0x4000, 0x4000, closed);
      }
      // The game allocates the guest page right after the texture.
      heap.Commit(0x6000, 0x1000, kRead | kWrite);
      // The texture's pages are as closed as the watch needs: a write to them still faults.
      assert(HostOf(heap, 4) == closed && HostOf(heap, 5) == closed);
      // A host page with no watch on it opens for the new allocation.
      heap.Commit(0x10000, 0x3000, kRead | kWrite);
      assert(HostOf(heap, 16) == Access::kReadWrite && HostOf(heap, 18) == Access::kReadWrite);
      // A commit across three host pages, the middle one watched: only that one stays closed.
      std::fill(heap.watch.begin(), heap.watch.end(), 0);
      const size_t middle = (host_offset + 0x1C000) / 0x4000;
      heap.watch[middle] = kind;
      heap.Commit(0x19000, 0x7000, kRead | kWrite);
      for (uint32_t page = 0x19; page < 0x20; ++page) {
        const bool in_middle = (host_offset + page * 0x1000) / 0x4000 == middle;
        assert(HostOf(heap, page) == (in_middle ? closed : Access::kReadWrite));
      }
    }
  }
  // A host with the guest's page size commits exactly what was asked, as before.
  memory::native = 0x1000; memory::commits = memory::protects = 0;
  BaseHeap plain; plain.watch[1] = 1;
  plain.Commit(0x5000, 0x1000, kRead | kWrite);
  assert(memory::commits == 1 && memory::protects == 0 && HostOf(plain, 5) == Access::kReadWrite && HostOf(plain, 4) == Access::kNoAccess);
  std::puts("PASS: a commit beside watched memory leaves the watch closed (write and read watches, heap offsets 0 and 0x1000); 4 KiB hosts unchanged");
}
'''
(out / "check.cpp").write_text(harness)
subprocess.run(["clang++-18", "-std=c++20", "-O1", "-g", "-fsanitize=address,undefined",
                str(out / "check.cpp"), "-o", str(out / "check")], check=True)
subprocess.run([str(out / "check")], check=True)
