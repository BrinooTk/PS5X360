"""Exercise the production RequestRange implementation with controlled invalidations."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
source=(root/'.deps/xenia-canary/src/xenia/gpu/shared_memory.cc').read_text()
a=source.index('bool SharedMemory::RequestRange(')
body=source[a:source.index('bool SharedMemory::IsRangeValid(',a)]
out=root/'build/shared-memory-cache-check';out.mkdir(parents=True,exist_ok=True)
harness=r"""
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <array>
#include <functional>
#include <random>
#include "xbox360ps5/gpu_diagnostics.hpp"
#include "xbox360ps5/validated_range_cache.hpp"
#define XE_PLATFORM_PS5 1
#define SCOPE_profile_cpu_f(x)
inline void ClearUnitBit(std::vector<uint64_t>& bits,uint32_t unit){bits[unit>>6]&=~(uint64_t(1)<<(unit&63));}
struct SharedMemory {
static constexpr uint32_t kBufferSize=128*4096;
unsigned page_size_log2_=12;
std::vector<std::pair<uint32_t,uint32_t>> upload_ranges_=std::vector<std::pair<uint32_t,uint32_t>>(128);
std::array<bool,128> valid{};
uint64_t bits[2]{};uint64_t* system_page_flags_valid_=bits;uint64_t lock_free_hits_=0;
bool UnwatchedPagesEnabled() const {return false;}
bool request_allows_unwatched_=false;
std::vector<std::pair<uint32_t,uint32_t>> unwatched_filtered_;
std::vector<uint64_t> unwatched_seen_bits_=std::vector<uint64_t>(2);
unsigned FilterUnwatched(std::pair<uint32_t,uint32_t>*&,unsigned count){return count;}
void Sync(unsigned i){if(valid[i])bits[i/64]|=uint64_t(1)<<(i%64);else bits[i/64]&=~(uint64_t(1)<<(i%64));}
std::array<unsigned,128> cpu{},gpu{};
xbox360ps5::ValidatedRangeCache request_cache_;
unsigned ensure_calls=0,uploads=0,locks=0;
bool fail=false,race_upload=false;
struct Lock {unsigned* n; int Acquire(){++*n;return 0;}} global_critical_region_{&locks};
bool RequestRange(uint32_t,uint32_t,bool allow_unwatched=false);
bool EnsureHostGpuMemoryAllocated(uint32_t,uint32_t){++ensure_calls;return !fail;}
void TryFindUploadRange(uint32_t,uint32_t,uint32_t first,uint32_t last,uint32_t& range,unsigned& count,std::pair<uint32_t,uint32_t>* out){
 range=UINT32_MAX;
 for(unsigned i=first;i<=last;++i)if(!valid[i])out[count++]={i,1};
}
bool UploadRanges(std::pair<uint32_t,uint32_t>* out,unsigned count){
 if(fail)return false;
 for(unsigned j=0;j<count;++j){uploads+=out[j].second;for(unsigned i=out[j].first;i<out[j].first+out[j].second;++i){valid[i]=true;Sync(i);gpu[i]=cpu[i];}}
 if(race_upload){request_cache_.Invalidate();valid[0]=false;Sync(0);race_upload=false;}
 return true;
}
void Invalidate(unsigned p){request_cache_.Invalidate();valid[p]=false;Sync(p);++cpu[p];}
};
"""+body+r"""
int main(){
using namespace xbox360ps5;
gpu_diag::enabled=true;
SharedMemory h;
for(unsigned i=0;i<1000;++i)assert(h.RequestRange(0,4096));
std::printf("Repeated resident range: %u residency checks, %u locks, %u uploads\n",h.ensure_calls,h.locks,h.uploads);std::fflush(stdout);
assert(h.ensure_calls==1 && h.locks==1 && h.uploads==1);
h.Invalidate(0);assert(h.RequestRange(0,4096));assert(h.uploads==2);
// Invalidation during an upload must prevent publishing a stale cache entry.
SharedMemory racing;racing.race_upload=true;assert(racing.RequestRange(0,4096));assert(!racing.valid[0]);
assert(racing.RequestRange(0,4096));assert(racing.uploads==2 && racing.valid[0]);
// Errors, empty ranges and boundary checks retain their original outcomes.
SharedMemory bad;bad.fail=true;assert(!bad.RequestRange(0,4096));bad.fail=false;assert(bad.RequestRange(0,4096));
assert(h.RequestRange(123,0));assert(!h.RequestRange(SharedMemory::kBufferSize,1));assert(!h.RequestRange(UINT32_MAX,128));
// Differential requests interleaved with guest writes/cache resets.
SharedMemory base,candidate;std::mt19937 rng(23);
for(unsigned i=0;i<10000;++i){
 if(rng()%5==0){unsigned page=rng()%128;base.Invalidate(page);candidate.Invalidate(page);}
 uint32_t start=(rng()%128)*4096, length=1+rng()%4096;
 gpu_diag::enabled=false;bool expected=base.RequestRange(start,length);
 gpu_diag::enabled=true;bool actual=candidate.RequestRange(start,length);
 assert(actual==expected && base.valid==candidate.valid && base.uploads==candidate.uploads && base.gpu==candidate.gpu);
}
// Clearing validity after a simulated reset cannot reuse an old validation.
h.request_cache_.Invalidate();h.valid.fill(false);h.bits[0]=h.bits[1]=0;assert(h.RequestRange(0,4096));assert(h.uploads==3);
// The bitmap helper itself: single bits, word boundaries and whole words.
{uint64_t w[3]={0,0,0};
 assert(!gpu_diag::RangeBitsAllSet(w,0,0));w[0]=1;assert(gpu_diag::RangeBitsAllSet(w,0,0)&&!gpu_diag::RangeBitsAllSet(w,0,1));
 w[0]=UINT64_MAX;w[1]=UINT64_MAX;w[2]=1;assert(gpu_diag::RangeBitsAllSet(w,0,128)&&!gpu_diag::RangeBitsAllSet(w,0,129));
 w[1]&=~(uint64_t(1)<<63);assert(gpu_diag::RangeBitsAllSet(w,5,126)&&!gpu_diag::RangeBitsAllSet(w,5,127)&&gpu_diag::RangeBitsAllSet(w,128,128));
 for(unsigned a=0;a<192;++a)for(unsigned b=a;b<192;++b){bool all=true;for(unsigned i=a;i<=b;++i)all=all&&((w[i/64]>>(i%64))&1);assert(gpu_diag::RangeBitsAllSet(w,a,b)==all);}}
// More distinct valid ranges than the exact cache holds: with the lock-free
// answer on, repeating them takes the lock no more; with it off, every miss does.
{SharedMemory many;for(unsigned p=0;p<100;++p)assert(many.RequestRange(p*4096,4096));
 unsigned before=many.locks;for(unsigned round=0;round<10;++round)for(unsigned p=0;p<100;++p)assert(many.RequestRange(p*4096,4096));
 std::printf("1000 repeats of 100 valid ranges: %u locks with the lock-free answer on\n",many.locks-before);
 assert(many.locks==before && many.lock_free_hits_>0);  // the exact-range cache answers the rest
 gpu_diag::lock_free_valid=false;before=many.locks;for(unsigned round=0;round<10;++round)for(unsigned p=0;p<100;++p)assert(many.RequestRange(p*4096,4096));
 std::printf("the same with it off: %u locks\n",many.locks-before);assert(many.locks-before>=300);gpu_diag::lock_free_valid=true;
 many.Invalidate(7);unsigned uploads=many.uploads;assert(many.RequestRange(7*4096,4096));assert(many.uploads==uploads+1);
 uploads=many.uploads;assert(many.RequestRange(6*4096,3*4096));assert(many.uploads==uploads);}
// The differential run again with the lock-free answer switched at random.
{SharedMemory base2,candidate2;std::mt19937 rng2(71);
 for(unsigned i=0;i<10000;++i){
  if(rng2()%5==0){unsigned page=rng2()%128;base2.Invalidate(page);candidate2.Invalidate(page);}
  uint32_t start=(rng2()%128)*4096+rng2()%4096, length=1+rng2()%20000;
  if(start+length>SharedMemory::kBufferSize)length=SharedMemory::kBufferSize-start;
  gpu_diag::enabled=false;bool expected=base2.RequestRange(start,length);
  gpu_diag::enabled=true;gpu_diag::lock_free_valid=rng2()%4!=0;bool actual=candidate2.RequestRange(start,length);
  assert(actual==expected && base2.valid==candidate2.valid && base2.uploads==candidate2.uploads && base2.gpu==candidate2.gpu);
 }gpu_diag::lock_free_valid=true;}
std::puts("PASS: 10000 differential requests, writes, resets, upload races and boundaries, with and without the lock-free answer");
}
"""
(out/'check.cpp').write_text(harness)
subprocess.run(['clang++-18','-std=c++20','-O1','-g','-fsanitize=address,undefined','-I'+str(root/'include'),str(out/'check.cpp'),'-o',str(out/'check')],check=True)
subprocess.run([str(out/'check')],check=True)
