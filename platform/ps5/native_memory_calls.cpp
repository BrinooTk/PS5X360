// SPDX-License-Identifier: MIT
// Native-title bridge to ordinary libkernel APIs, for this process only.
// Avoid payload-SDK mmap/mprotect helpers; never change process privileges.
#include <cerrno>
#include <cstddef>
#include <sys/mman.h>
#include <sys/types.h>
extern "C" {
int sceKernelMmap(void*, size_t, int, int, int, off_t, void**);
int sceKernelMprotect(const void*, size_t, int);
int sceKernelMunmap(void*, size_t);
}
namespace {
int PosixResult(int status) {
  if (!status) return 0;
  // SCE kernel errors encode the BSD errno in the low 16 bits.
  errno = static_cast<unsigned>(status) & 0xffff;
  if (!errno) errno = EIO;
  return -1;
}
}
extern "C" void* __wrap_mmap(void* address, size_t length, int protection,
                             int flags, int descriptor, off_t offset) {
  void* mapped = nullptr;
  if (PosixResult(sceKernelMmap(address,length,protection,flags,descriptor,offset,&mapped)))
    return MAP_FAILED;
  return mapped;
}
extern "C" int __wrap_mprotect(void* address, size_t length, int protection) {
  return PosixResult(sceKernelMprotect(address,length,protection));
}
extern "C" int __wrap_munmap(void* address, size_t length) {
  return PosixResult(sceKernelMunmap(address,length));
}
