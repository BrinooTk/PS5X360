"""Compare real callback-arming code in baseline and experimental modes."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
source = (root / ".deps/xenia-canary/src/xenia/memory.cc").read_text()
start = source.index("template <bool enable_invalidation_notifications, bool enable_data_providers>\nXE_NOINLINE void PhysicalHeap::EnableAccessCallbacksInner")
body = source[start:source.index("bool PhysicalHeap::TriggerCallbacks(", start)]
out = root / "build/watch-batching-check"
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <random>
#include "xbox360ps5/gpu_diagnostics.hpp"
#define XE_PLATFORM_PS5 1
#define XE_ARCH_AMD64 0
#define XE_NOINLINE
#define XE_RESTRICT
namespace xe::memory {
enum class PageAccess {kNoAccess=0,kReadOnly=1,kReadWrite=3};
bool Protect(void*,size_t,PageAccess);
}
using Access=xe::memory::PageAccess;
struct SystemPageFlagsBlock {uint64_t notify_on_invalidation=0,notify_on_read=0;};
struct PhysicalHeap {
std::vector<uint8_t> backing=std::vector<uint8_t>(32*0x4000);
uint8_t* membase_=backing.data();unsigned heap_base_=0,system_page_shift_=14;
std::vector<SystemPageFlagsBlock> system_page_flags_=std::vector<SystemPageFlagsBlock>(1);
std::vector<Access> guest=std::vector<Access>(32,Access::kReadWrite),host=guest;
size_t calls=0;
Access SystemPageGuestAccess(unsigned i){return guest[i];}
template<bool W,bool R> void EnableAccessCallbacksInner(uint32_t,uint32_t,Access);
};
PhysicalHeap* active;
bool xe::memory::Protect(void* p,size_t bytes,Access target) {
assert(bytes && bytes%0x4000==0);++active->calls;
const size_t first=(static_cast<uint8_t*>(p)-active->membase_)/0x4000;
for(size_t i=first;i<first+bytes/0x4000;++i)active->host[i]=target;
return true;
}
''' + body + r'''
void run(PhysicalHeap& h,bool experimental,bool writes,bool reads) {
active=&h;xbox360ps5::gpu_diag::enabled=experimental;
if(writes && reads)h.EnableAccessCallbacksInner<true,true>(0,31,Access::kNoAccess);
else if(writes)h.EnableAccessCallbacksInner<true,false>(0,31,Access::kReadOnly);
else h.EnableAccessCallbacksInner<false,true>(0,31,Access::kNoAccess);
}
int main() {
PhysicalHeap h;
for(unsigned i=1;i<32;i+=2){h.system_page_flags_[0].notify_on_invalidation|=uint64_t(1)<<i;h.host[i]=Access::kReadOnly;}
run(h,true,true,false);
std::printf("Alternating already-watched pages: %zu protection calls\n",h.calls);
if(h.calls!=1)return 1;
// Differential test: mixed genuine guest permissions and read/write watches.
std::mt19937 rng(42);
for(unsigned trial=0;trial<1000;++trial) {
PhysicalHeap baseline,candidate;
for(unsigned i=0;i<32;++i) {
const auto access=Access((unsigned[]){0,1,3}[rng()%3]);
bool read=access!=Access::kNoAccess && rng()%2;
bool write=access==Access::kReadWrite && rng()%2;
baseline.guest[i]=candidate.guest[i]=access;
baseline.host[i]=candidate.host[i]=read ? Access::kNoAccess : write ? Access::kReadOnly : access;
if(read)baseline.system_page_flags_[0].notify_on_read|=uint64_t(1)<<i;
if(write)baseline.system_page_flags_[0].notify_on_invalidation|=uint64_t(1)<<i;
}
candidate.system_page_flags_=baseline.system_page_flags_;
bool writes=trial%3!=2,reads=trial%3!=0;
run(baseline,false,writes,reads);run(candidate,true,writes,reads);
assert(baseline.host==candidate.host);
assert(baseline.system_page_flags_[0].notify_on_read==candidate.system_page_flags_[0].notify_on_read);
assert(baseline.system_page_flags_[0].notify_on_invalidation==candidate.system_page_flags_[0].notify_on_invalidation);
assert(candidate.calls<=baseline.calls);
}
std::puts("PASS: 1000 mixed-permission/read-watch/write-watch cases match the baseline");
}
'''
(out / "check.cpp").write_text(harness)
subprocess.run(["clang++-18", "-std=c++20", "-O1", "-g", "-fsanitize=address,undefined",
                "-I" + str(root / "include"), str(out / "check.cpp"), "-o", str(out / "check")], check=True)
subprocess.run([str(out / "check")], check=True)
