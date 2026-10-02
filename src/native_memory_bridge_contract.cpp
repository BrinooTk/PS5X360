// SPDX-License-Identifier: MIT
// ABI and error propagation for the native-title memory bridge; no hardware.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include <sys/types.h>
extern "C" {
void* __wrap_mmap(void*,size_t,int,int,int,off_t);
int __wrap_mprotect(void*,size_t,int);
int __wrap_munmap(void*,size_t);
}
namespace {
int status = 0, seen_fd = 0, seen_flags = 0, seen_protection = 0;
off_t seen_offset = 0;
size_t seen_length = 0;
void* seen_address = nullptr;
unsigned cases = 0, failures = 0;
void Check(bool value,const char* name) {
  ++cases; failures += !value;
  std::printf("%s %s\n",value ? "PASS" : "FAIL",name);
}
}
extern "C" int sceKernelMmap(void* address,size_t length,int protection,int flags,int fd,off_t offset,void** result) {
  seen_address = address; seen_length = length; seen_protection = protection;
  seen_flags = flags; seen_fd = fd; seen_offset = offset;
  *result = reinterpret_cast<void*>(uintptr_t(0x1234000));
  return status;
}
extern "C" int sceKernelMprotect(const void* address,size_t length,int protection) {
  seen_address = const_cast<void*>(address); seen_length = length; seen_protection = protection;
  return status;
}
extern "C" int sceKernelMunmap(void* address,size_t length) {
  seen_address = address; seen_length = length; return status;
}
int main() {
  void* hint = reinterpret_cast<void*>(uintptr_t(0x900000000));
  const auto mapped = __wrap_mmap(hint,0x4000,PROT_READ | PROT_WRITE,MAP_SHARED,7,off_t(0x100000000ull));
  Check(mapped == reinterpret_cast<void*>(uintptr_t(0x1234000)),"use native output pointer, not return status");
  Check(seen_offset == off_t(0x100000000ull) && seen_fd == 7 && seen_flags == MAP_SHARED &&
        seen_address == hint && seen_length == 0x4000 && seen_protection == (PROT_READ | PROT_WRITE),"preserve 64-bit offset and mapping arguments");
  status = static_cast<int>(0x80020000u | EACCES);
  Check(__wrap_mmap(nullptr,0x4000,PROT_EXEC,MAP_PRIVATE | MAP_ANONYMOUS,-1,0) == MAP_FAILED && errno == EACCES,"propagate executable mapping denial without fallback");
  status = 0;
  Check(__wrap_mprotect(mapped,0x4000,PROT_READ) == 0 && seen_address == mapped && seen_length == 0x4000 && seen_protection == PROT_READ,"native protection call receives requested region");
  status = static_cast<int>(0x80020000u | EACCES);
  Check(__wrap_mprotect(mapped,0x4000,PROT_EXEC) == -1 && errno == EACCES,"protection denial is not reported as success");
  status = 0;
  Check(__wrap_munmap(mapped,0x4000) == 0 && seen_address == mapped && seen_length == 0x4000,"native unmap receives original mapping");
  status = static_cast<int>(0x80020000u | EINVAL);
  Check(__wrap_munmap(mapped,0x4000) == -1 && errno == EINVAL,"propagate invalid unmap error");
  std::printf("NATIVE MEMORY BRIDGE CASES %u FAILURES %u (mock native APIs)\n",cases,failures);
  return failures ? 1 : 0;
}
