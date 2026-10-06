"""Exercise the actual PS5 Apply function with a fake ordinary kernel API."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
source = (root / "platform/ps5/memory_ps5.cpp").read_text()
body = source[source.index("bool Apply("):source.index("bool SetAccess(")]
out = root / "build/protection-batching-check"
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <vector>
#include "xbox360ps5/gpu_diagnostics.hpp"
namespace xbox360ps5 {
std::atomic<unsigned long long> protect_syscalls{0}, protect_nanoseconds{0}, protect_pages{0}, open_syscalls{0}, open_nanoseconds{0};
}
constexpr size_t kPage=0x4000,kPerPage=4;
constexpr uint8_t kNoHold=0xff;
struct View { std::vector<uint8_t> access,host,hold; };
struct Call {uintptr_t address;size_t bytes;int bits;};
std::vector<Call> calls;
bool fail=false;
int32_t sceKernelMprotect(const void* address,size_t bytes,int bits) {
  calls.push_back({uintptr_t(address),bytes,bits});return fail ? -1 : 0;
}
void Note(const char*,uintptr_t,size_t,int) {}
''' + body + r'''
int main() {
  const uintptr_t base=0x1000000000ull;
  xbox360ps5::gpu_diag::enabled=true;
  View v{std::vector<uint8_t>(64*4,1),std::vector<uint8_t>(64,1),std::vector<uint8_t>(64,kNoHold)};
  // Alternating pages already at the target: one identical-protection run.
  for(size_t i=0;i<64;i+=2)v.host[i]=3;
  assert(Apply(base,v,0,64*4));
  for(auto bits:v.host)assert(bits==1);
  std::printf("Alternating already-protected pages: %zu kernel calls\n",calls.size());
  if(calls.size()!=1)return 1;
  assert(calls[0].address==base && calls[0].bytes==63*kPage);
  // Distinct target protections are hard boundaries, never merged.
  calls.clear();v.host.assign(64,3);v.hold[1]=0;
  assert(Apply(base,v,0,12));
  assert(calls.size()==3 && calls[0].bits==1 && calls[1].bits==0 && calls[2].bits==1);
  assert(v.host[3]==3);
  // Failed kernel calls never mark a changed range as successfully protected.
  calls.clear();v.host.assign(64,3);v.hold.assign(64,kNoHold);fail=true;
  assert(!Apply(base,v,0,8));
  assert(v.host[0]==3 && v.host[1]==3);fail=false;
  // Disabled experiment preserves the original call pattern.
  xbox360ps5::gpu_diag::enabled=false;calls.clear();v.host.assign(64,1);
  for(size_t i=0;i<64;i+=2)v.host[i]=3;
  assert(Apply(base,v,0,64*4));assert(calls.size()==32);
  std::puts("PASS: batched equivalent protections, boundaries, failure state and baseline mode");
}
'''
(out / "check.cpp").write_text(harness)
subprocess.run(["clang++-18", "-std=c++20", "-O1", "-g", "-fsanitize=address,undefined",
                "-I" + str(root / "include"), str(out / "check.cpp"), "-o", str(out / "check")], check=True)
subprocess.run([str(out / "check")], check=True)
