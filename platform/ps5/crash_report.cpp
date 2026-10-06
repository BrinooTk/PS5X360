// SPDX-License-Identifier: MIT
// Boot breadcrumbs and a fatal-signal report for the native title. The handler
// uses only system calls and static buffers. Code addresses are printed as
// eboot+offset, which is the address in the build's llvm-pie.elf.
#include "xbox360ps5/crash_report.hpp"
#include <cstring>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <initializer_list>
#include <pthread.h>
#include <cerrno>
#include <cstdio>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
extern "C" {
int sceKernelGetCurrentCpu(void);
int sceKernelDebugOutText(int, const char*);
int sceKernelVirtualQuery(const void*, int, void*, size_t);
int64_t sceKernelGetDirectMemorySize();
int32_t sceKernelAvailableDirectMemorySize(int64_t, int64_t, size_t, int64_t*, size_t*);
int32_t sceKernelAvailableFlexibleMemorySize(size_t*);
int32_t sceKernelConfiguredFlexibleMemorySize(size_t*);
int32_t sceKernelReserveVirtualRange(void**, size_t, int, size_t);
int32_t sceKernelMunmap(void*, size_t);
void _start();
extern const char __eh_frame_hdr_start[];  // First byte after the code segment.
}
extern "C" int ps5___cxa_thread_atexit_impl(void (*)(void*), void*, void*);
namespace {
// thread_local storage on this target is emulated: a pthread key's destructor
// frees a thread's variables when it exits. The C++ destructors of those
// variables run from another key's destructor (the platform's
// __cxa_thread_atexit_impl), and keys are destroyed in creation order. Create
// the destructor key before any thread_local is touched, so objects are
// destroyed before their storage is freed.
__attribute__((constructor(101))) void OrderThreadExitKeys() {
  pthread_key_t probe;
  const int created = pthread_key_create(&probe, nullptr);
  if (!created) pthread_key_delete(probe);
  ps5___cxa_thread_atexit_impl([](void*) {}, nullptr, nullptr);
  char text[96];
  std::snprintf(text, sizeof(text), "[X360] BOOT thread-exit key ordered; first free key was %d\n",
                created ? -1 : int(probe));
  sceKernelDebugOutText(0, text);
}
}
namespace xbox360ps5 {
namespace {
constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS, SIGTRAP};
int report_file = -1;
int probe_pipe[2] = {-1, -1};
std::atomic<bool> reporting{false};
char line[256];
size_t used = 0;

uintptr_t CodeStart() { return reinterpret_cast<uintptr_t>(&_start) & ~uintptr_t(0x3fff); }
uintptr_t CodeEnd() { return reinterpret_cast<uintptr_t>(__eh_frame_hdr_start); }
bool InCode(uint64_t address) { return address >= CodeStart() && address < CodeEnd(); }
void Put(char c) { if (used + 2 < sizeof(line)) line[used++] = c; }
void Put(const char* text) { for (; *text; ++text) Put(*text); }
void Hex(uint64_t value) {
  char digits[16]; int count = 0;
  do { digits[count++] = "0123456789abcdef"[value & 15]; value >>= 4; } while (value);
  while (count) Put(digits[--count]);
}
void Address(uint64_t address) {
  if (InCode(address)) { Put("eboot+0x"); Hex(address - CodeStart()); }
  else { Put("0x"); Hex(address); }
}
void Flush() {
  Put('\n'); line[used] = 0;
  // Signal handlers cannot take NetLog's mutex or allocate its std::string.
  // An interrupted logger may already hold that mutex. Persist first; leave
  // network buffering to normal execution rather than risking a second hang.
  if (report_file >= 0) (void)!write(report_file, line, used);
  sceKernelDebugOutText(0, line);
  used = 0;
}
// The kernel answers an unreadable address with an error instead of a fault.
bool Copy(uint64_t address, void* out, size_t bytes) {
  if (probe_pipe[1] < 0) return false;
  if (write(probe_pipe[1], reinterpret_cast<const void*>(address), bytes) != ssize_t(bytes)) return false;
  return read(probe_pipe[0], out, bytes) == ssize_t(bytes);
}
void Handle(int number, siginfo_t* info, void* context) {
  struct sigaction standard{};
  standard.sa_handler = SIG_DFL;
  sigemptyset(&standard.sa_mask);
  // Returning repeats the fault under the default action, which also gives
  // the system's own dump. A second fault inside this handler ends the same way.
  for (int signal : kSignals) sigaction(signal, &standard, nullptr);
  if (reporting.exchange(true)) return;
  // The console's context: FreeBSD's machine context after 64 bytes.
  const uint64_t* m = static_cast<const uint64_t*>(context) + 8;
  used = 0;
  Put("[X360] CRASH signal="); Hex(uint64_t(number));
  Put(" code="); Hex(uint64_t(info ? info->si_code : 0));
  Put(" addr="); Address(info ? reinterpret_cast<uint64_t>(info->si_addr) : 0);
  Put(" rip="); Address(m[20]); Put(" trap="); Hex(m[16] & 0xffffffffu); Put(" err="); Hex(m[19]);
  Flush();
  const char* names[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9", "rax", "rbx", "rbp",
                         "r10", "r11", "r12", "r13", "r14", "r15"};
  Put("[X360]");
  for (int n = 0; n < 15; ++n) {
    Put(' '); Put(names[n]); Put('='); Address(m[1 + n]);
    if (n == 7) { Flush(); Put("[X360]"); }
  }
  Put(" rsp="); Hex(m[23]); Put(" rflags="); Hex(m[22]);
  Flush();
  Put("[X360] context length="); Hex(m[25]); Put(" fpformat="); Hex(m[26]); Put(" ownedfp="); Hex(m[27]);
  Flush();
  uint8_t code[16];
  if (Copy(m[20], code, sizeof(code))) {
    Put("[X360] code:");
    for (uint8_t byte : code) { Put(' '); if (byte < 16) Put('0'); Hex(byte); }
    Flush();
  }
  // Stack words that point into the code are the likely return addresses.
  int found = 0;
  for (uint64_t at = m[23] & ~uint64_t(7); found < 40 && at < (m[23] & ~uint64_t(7)) + 0x4000; at += 8) {
    uint64_t value;
    if (!Copy(at, &value, sizeof(value))) break;
    if (!InCode(value)) continue;
    if (found % 6 == 0) { if (found) Flush(); Put("[X360] stack:"); }
    Put(' '); Address(value); ++found;
  }
  Flush();
  // No network-drain wait from a fatal signal handler.
}
// Where a thread is: asked of it with a signal, answered from its own context.
std::atomic<bool> probe_done{true};
const char* probe_name = "";
// For a performance sample only the instruction is taken and, when it is
// outside the title's code and the generated code (a system library), the
// nearest return address into the title: what called it.
std::atomic<ThreadSample*> sample_out{nullptr};
void Probe(int, siginfo_t*, void* context) {
  const uint64_t* m = static_cast<const uint64_t*>(context) + 8;
  if (ThreadSample* out = sample_out.load()) {
    out->rip = m[20];
    out->caller = out->caller2 = out->slot = 0;
    out->cpu = sceKernelGetCurrentCpu();
    out->in_title = InCode(m[20]);
    if (!out->in_title && !(m[20] >= 0x40000000 && m[20] < 0x50000000)) {
      for (uint64_t at = m[23] & ~uint64_t(7), end = at + 0x300; at < end; at += 8) {
        uint64_t value;
        if (!Copy(at, &value, sizeof(value))) break;
        if (!InCode(value)) continue;
        if (!out->caller) {
          out->caller = value - CodeStart();
          // The call that returns here, when it is `call *disp32(%rip)`.
          uint8_t code[6];
          if (value - CodeStart() >= 6 && Copy(value - 6, code, sizeof(code)) && code[0] == 0xFF && code[1] == 0x15) {
            int32_t displacement;
            std::memcpy(&displacement, code + 2, sizeof(displacement));
            out->slot = value + int64_t(displacement) - CodeStart();
          }
        }
        else if (value - CodeStart() != out->caller) { out->caller2 = value - CodeStart(); break; }
      }
    }
    if (out->in_title) out->rip -= CodeStart();
    probe_done = true;
    return;
  }
  used = 0;
  Put("[X360] WHERE "); Put(probe_name); Put(" rip="); Address(m[20]); Put(" rsp="); Hex(m[23]);
  int found = 0;
  for (uint64_t at = m[23] & ~uint64_t(7); found < 14 && at < (m[23] & ~uint64_t(7)) + 0x2000; at += 8) {
    uint64_t value;
    if (!Copy(at, &value, sizeof(value))) break;
    if (!InCode(value)) continue;
    Put(' '); Address(value); ++found;
  }
  Flush();
  probe_done = true;
}
}
namespace {
void InstallProbe() {
  static bool installed = false;
  if (installed) return;
  struct sigaction action{};
  action.sa_sigaction = Probe;
  action.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&action.sa_mask);
  sigaction(SIGUSR2, &action, nullptr);
  installed = true;
}
}
bool SampleThread(void* thread, ThreadSample* out) {
  InstallProbe();
  if (!thread || !out) return false;
  sample_out = out;
  probe_done = false;
  bool taken = false;
  if (!pthread_kill(reinterpret_cast<pthread_t>(thread), SIGUSR2)) {
    for (int wait = 0; wait < 400 && !probe_done; ++wait) usleep(50);
    taken = probe_done;
    // A late answer must not write into a sample that is gone.
    if (!taken) for (int wait = 0; wait < 100 && !probe_done; ++wait) usleep(1000);
  }
  sample_out = nullptr;
  return taken;
}
void ProbeThread(void* thread, const char* name) {
  InstallProbe();
  if (!thread) return;
  probe_name = name;
  probe_done = false;
  if (pthread_kill(reinterpret_cast<pthread_t>(thread), SIGUSR2)) return;
  for (int wait = 0; wait < 200 && !probe_done; ++wait) usleep(1000);
}
void InstallCrashReport(const char* path) {
  report_file = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (report_file < 0) report_file = open("/download0/xbox360ps5/boot.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
  (void)!pipe(probe_pipe);
  struct sigaction action{};
  action.sa_sigaction = Handle;
  action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);
  for (int signal : kSignals) sigaction(signal, &action, nullptr);
}
bool SetCrashReportFile(const char* path) {
  const int next = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (next < 0) return false;
  // Keep the descriptor number stable for concurrent Stage/signal writes.
  const bool changed = report_file >= 0 && dup2(next, report_file) >= 0;
  close(next);
  return changed;
}
void Stage(const char* text) {
  char buffer[200];
  size_t size = 0;
  for (const char* prefix = "[X360] "; *prefix; ++prefix) buffer[size++] = *prefix;
  for (; *text && size + 2 < sizeof(buffer); ++text) buffer[size++] = *text;
  buffer[size++] = '\n'; buffer[size] = 0;
  sceKernelDebugOutText(0, buffer);
  NetLog(buffer, size);
  if (report_file >= 0) { (void)!write(report_file, buffer, size); fsync(report_file); }
}
void ReportPlatformMemory() {
  char text[180];
  size_t flexible = 0, configured = 0, direct_free = 0;
  int64_t direct_at = 0;
  const int64_t direct = sceKernelGetDirectMemorySize();
  const int rc_free = sceKernelAvailableDirectMemorySize(0, direct, 0x10000, &direct_at, &direct_free);
  const int rc_flexible = sceKernelAvailableFlexibleMemorySize(&flexible);
  const int rc_configured = sceKernelConfiguredFlexibleMemorySize(&configured);
  std::snprintf(text, sizeof(text), "MEM page=%ld direct=%llx free=%llx(rc %x) flexible=%llx(rc %x) configured=%llx(rc %x)",
      sysconf(_SC_PAGESIZE), (unsigned long long)direct, (unsigned long long)direct_free, unsigned(rc_free),
      (unsigned long long)flexible, unsigned(rc_flexible), (unsigned long long)configured, unsigned(rc_configured));
  Stage(text);
  std::snprintf(text, sizeof(text), "MEM code=%llx-%llx stack=%p", (unsigned long long)CodeStart(),
      (unsigned long long)CodeEnd(), static_cast<void*>(text));
  Stage(text);
  // The address-space layout decides where guest memory and the JIT tables can go.
  struct Info { void* start; void* end; int64_t offset; int protection; int type; unsigned flags; char name[32]; } info;
  uintptr_t at = 0;
  for (int n = 0; n < 160; ++n) {
    info = {};
    if (sceKernelVirtualQuery(reinterpret_cast<void*>(at), 1, &info, sizeof(info)) != 0) break;
    info.name[31] = 0;
    std::snprintf(text, sizeof(text), "MAP %012llx-%012llx prot=%x type=%x flags=%x %s",
        (unsigned long long)uintptr_t(info.start), (unsigned long long)uintptr_t(info.end),
        unsigned(info.protection), unsigned(info.type), info.flags & 0x1f, info.name);
    Stage(text);
    if (uintptr_t(info.end) <= at) break;
    at = uintptr_t(info.end);
  }
}
}
