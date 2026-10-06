"""Exercise production invalidation with GPU-written neighbors and exact writes."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
source = (root / '.deps/xenia-canary/src/xenia/gpu/shared_memory.cc').read_text()
start = source.index('std::pair<uint32_t, uint32_t> SharedMemory::MemoryInvalidationCallback(')
body = source[start:source.index('void SharedMemory::PrepareForTraceDownload()', start)]
out = root / 'build/invalidation-window-check'
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include "xbox360ps5/gpu_diagnostics.hpp"
#include "xbox360ps5/validated_range_cache.hpp"
#define XE_PLATFORM_PS5 1
namespace xe {
uint8_t lzcnt(uint64_t v){return std::countl_zero(v);}
uint8_t tzcnt(uint64_t v){return std::countr_zero(v);}
}
struct SharedMemory {
static constexpr uint32_t kBufferSize=512*4096;
unsigned page_size_log2_=12;
std::array<uint64_t,8> system_page_flags_valid_,system_page_flags_valid_and_gpu_written_;
xbox360ps5::ValidatedRangeCache request_cache_;
struct Lock {int Acquire(){return 0;}} global_critical_region_;
uint32_t fired_first=0,fired_last=0;
SharedMemory(){system_page_flags_valid_.fill(UINT64_MAX);system_page_flags_valid_and_gpu_written_.fill(0);}
void FireWatches(uint32_t a,uint32_t b,bool){fired_first=a;fired_last=b;}
bool UnwatchedPagesEnabled() const {return false;}
void NoteCpuWrite(uint32_t,uint32_t){}
void ForgetUnwatched(uint32_t,uint32_t){}
std::pair<uint32_t,uint32_t> MemoryInvalidationCallback(uint32_t,uint32_t,bool);
};
''' + body + r'''
bool bit(const std::array<uint64_t,8>& a,unsigned p){return a[p/64]&(uint64_t(1)<<(p%64));}
int main(){
using namespace xbox360ps5;
gpu_diag::enabled=true;
gpu_diag::invalidation_window=0x10000;
SharedMemory hot;
auto window=hot.MemoryInvalidationCallback(8*4096,4*4096,false);
std::printf("Single host-page write invalidates %u bytes\n",window.second);std::fflush(stdout);
assert(window.first==0 && window.second==65536);
// A disjoint range in the old 256 KiB window must remain reusable.
assert(bit(hot.system_page_flags_valid_,40));
// The other selectable sizes: one host page (16 KiB), and the core's own block.
gpu_diag::invalidation_window=0x4000;
SharedMemory small;
auto page=small.MemoryInvalidationCallback(8*4096,4*4096,false);
std::printf("With a 16 KiB window the same write invalidates %u bytes\n",page.second);
assert(page.first==8*4096 && page.second==16384);
assert(bit(small.system_page_flags_valid_,7) && bit(small.system_page_flags_valid_,12));
gpu_diag::invalidation_window=0;
SharedMemory block;
auto whole=block.MemoryInvalidationCallback(8*4096,4*4096,false);
std::printf("With the window off it invalidates %u bytes\n",whole.second);
assert(whole.first==0 && whole.second==262144);
std::mt19937 rng(153);
for(unsigned trial=0;trial<10000;++trial){
 static constexpr uint32_t windows[]={0x10000,0x4000,0};
 const uint32_t window_bytes=windows[trial%3];
 const unsigned mask=window_bytes?window_bytes/4096-1:63;
 gpu_diag::invalidation_window=window_bytes;
 SharedMemory baseline,candidate;
 for(unsigned b=0;b<8;++b){
  uint64_t v=(uint64_t(rng())<<32)|rng();
  baseline.system_page_flags_valid_and_gpu_written_[b]=candidate.system_page_flags_valid_and_gpu_written_[b]=v;
 }
 const auto original=candidate.system_page_flags_valid_and_gpu_written_;
 unsigned first=rng()%512,length=1+rng()%std::min(80u,512-first),last=first+length-1;
 bool exact=trial%3==0;
 gpu_diag::enabled=false;auto old=baseline.MemoryInvalidationCallback(first*4096,length*4096,exact);
 gpu_diag::enabled=true;auto now=candidate.MemoryInvalidationCallback(first*4096,length*4096,exact);
 assert(now.first<=first*4096 && now.first+now.second>last*4096);
 assert(now.first>=old.first && now.first+now.second<=old.first+old.second);
 if(exact)assert(now==old && now.first==first*4096 && now.second==length*4096);
 else assert(now.first>=(first&~mask)*4096 && now.first+now.second<=(last|mask)*4096+4096);
 if(!window_bytes)assert(now==old);
 for(unsigned p=0;p<512;++p){
  bool inside=p>=now.first/4096 && p<(now.first+now.second)/4096;
  assert(bit(candidate.system_page_flags_valid_,p)==!inside);
  assert(bit(candidate.system_page_flags_valid_and_gpu_written_,p)==(bit(original,p)&&!inside));
  if(bit(original,p)&& (p<first || p>last))assert(!inside);
 }
 assert(candidate.fired_first==now.first/4096 && candidate.fired_last==(now.first+now.second)/4096-1);
}
// Invalid input and upper-buffer edge keep their existing contracts.
SharedMemory edge;auto zero=edge.MemoryInvalidationCallback(0,0,false);assert(zero.second==UINT32_MAX);
auto end=edge.MemoryInvalidationCallback(SharedMemory::kBufferSize-1,100,false);
assert(end.first+end.second==SharedMemory::kBufferSize);
gpu_diag::invalidation_window=0x4000;
std::puts("PASS: 10000 GPU-neighbor, exact-write, cross-window and buffer-edge cases over 64 KiB, 16 KiB and unlimited windows");
}
'''
(out / 'check.cpp').write_text(harness)
subprocess.run(['clang++-18','-std=c++20','-O1','-g','-fsanitize=address,undefined',
                '-I'+str(root/'include'),str(out/'check.cpp'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],check=True)
