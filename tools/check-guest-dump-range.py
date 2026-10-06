"""Exercise the actual diagnostic reader at the last mapped guest page."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
source = (root / 'include/xbox360ps5/guest_crash_dump.hpp').read_text()
body = source[source.index('inline void DumpGuestWords'):source.index('inline void DumpGuestCrash')]
test = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
static uint32_t ram[8]{};
static unsigned reads, rows;
namespace xe {
template<class T> using be = T;
struct Memory {
  void* TranslateVirtual(uint32_t at) {
    assert(at >= 0xFFFFFFE0u); ++reads;
    return reinterpret_cast<char*>(ram) + (at - 0xFFFFFFE0u);
  }
};
namespace memory {
enum class PageAccess { kNoAccess, kReadOnly };
bool QueryProtect(void*, size_t&, PageAccess& access) { access=PageAccess::kReadOnly; return true; }
}
}
template<class... T> void Log(const char* format, T...) {
  if (std::strstr(format, "not readable")) return;
  ++rows;
}
#define XELOGE Log
''' + body + r'''
int main() {
  xe::Memory memory;
  DumpGuestWords(&memory, "boundary", 0xFFFFFFF0u, 0x60);
  assert(reads==1 && rows==1);
  reads=rows=0;
  DumpGuestWords(&memory, "boundary", 0xFFFFFFE0u, 0x60);
  assert(reads==2 && rows==2);
  puts("PASS: diagnostic dump stops at 4 GiB without wrapping or dropping the last guest words");
}
'''
out = root / 'build/guest-dump-check'
out.mkdir(exist_ok=True)
(out / 'test.cc').write_text(test)
subprocess.run(['clang++-18','-std=c++20','-fsanitize=address,undefined',str(out/'test.cc'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],check=True)
