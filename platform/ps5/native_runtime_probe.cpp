// SPDX-License-Identifier: MIT
// M7 native hardware gate. Runs actual Xenia PPC -> x64, not a HIR oracle.
#include "demo_renderer.hpp"
#include <array>
#include <cstdio>
#include <exception>
#include <fcntl.h>
#include <unistd.h>

extern "C" {
int sceUserServiceInitialize(void*);
int sceUserServiceGetInitialUser(int*);
int sceUserServiceGetLoginUserIdList(int*);
int scePadInit();
int scePadOpen(int, int, int, void*);
int scePadGetHandle(int, int, int);
int scePadReadState(int, void*);
}
int RunActualRuntimeProbe(unsigned&, unsigned&);
namespace {
unsigned previous = 0, runs = 0, cases = 0, failures = 0;
int pad = -1, status = -1;
constexpr const char* log_path = "/download0/xbox360ps5-m7.log";
void Trace(const char* message) {
  int fd = open(log_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
  if (fd >= 0) {
    (void)write(fd, message, __builtin_strlen(message));
    (void)write(fd, "\n", 1);
    close(fd);
  }
}
int OpenPad() {
  sceUserServiceInitialize(nullptr);
  scePadInit();
  int first = -1;
  sceUserServiceGetInitialUser(&first);
  int users[4]{-1,-1,-1,-1};
  sceUserServiceGetLoginUserIdList(users);
  for (int user : {first,users[0],users[1],users[2],users[3],0xff}) {
    if (user < 0) continue;
    int handle = scePadOpen(user,0,0,nullptr);
    if (handle < 0) handle = scePadGetHandle(user,0,0);
    if (handle >= 0) return handle;
  }
  return -1;
}
void Run() {
  ++runs;
  if (chdir("/download0") != 0) {
    status = 10;
    Trace("M7 writable backing directory unavailable");
    return;
  }
  Trace("M7 actual runtime starting");
  try { status = RunActualRuntimeProbe(cases,failures); }
  catch (const std::exception& error) { status = 8; Trace(error.what()); }
  catch (...) { status = 9; Trace("unknown C++ exception"); }
  char line[120];
  std::snprintf(line,sizeof(line),"M7 runs=%u status=%d cases=%u failures=%u",runs,status,cases,failures);
  Trace(line);
}
void Draw(ps5::demo::Canvas& canvas) noexcept {
  using ps5::demo::Color;
  alignas(16) unsigned char sample[128]{};
  bool connected = pad >= 0 && scePadReadState(pad,sample) >= 0 && sample[76];
  unsigned buttons = connected ? unsigned(sample[0]) | (unsigned(sample[1]) << 8) |
      (unsigned(sample[2]) << 16) | (unsigned(sample[3]) << 24) : 0;
  if ((buttons & ~previous) & 0x4000) Run();
  previous = buttons;
  canvas.clear(Color::background);
  canvas.rectangle(80,70,1760,940,Color::panel);
  canvas.text(130,110,"XBOX360PS5 M7",7,Color::cyan);
  canvas.text(130,205,"ACTUAL PPC TO X64 RUNTIME",4,Color::white);
  canvas.text(130,280,"SYNTHETIC CPU TEST - NO GAME OR GPU",3,Color::yellow);
  bool pass = status == 0 && cases == 50 && failures == 0;
  canvas.text(130,380,status < 0 ? "PRESS X TO EXECUTE" : pass ? "CPU JIT PASS" : "CPU JIT FAILED",5,pass ? Color::cyan : Color::yellow);
  char line[120];
  std::snprintf(line,sizeof(line),"CASES %u FAILURES %u RUNS %u",cases,failures,runs);
  canvas.text(130,490,line,4,Color::white);
  std::snprintf(line,sizeof(line),"STATUS %d",status);
  canvas.text(130,570,line,3,Color::white);
  canvas.text(130,650,connected ? "DUALSENSE READY" : "PAD NOT CONNECTED",3,Color::cyan);
  canvas.text(130,735,"LOG DOWNLOAD0 XBOX360PS5-M7.LOG",3,Color::white);
  canvas.text(130,840,"PRESS X AGAIN TO CHECK CLEAN REINITIALIZATION",3,Color::white);
  canvas.text(130,910,"CLOSE FROM THE PS5 HOME SCREEN",3,Color::white);
}
}
int main() {
  // Persist both upstream test output and diagnostics. Keep the UI alive on a
  // returned initialization failure; never label zero executed cases as PASS.
  (void)std::freopen(log_path,"a",stdout);
  (void)std::freopen(log_path,"a",stderr);
  std::setvbuf(stdout,nullptr,_IONBF,0);
  std::setvbuf(stderr,nullptr,_IONBF,0);
  Trace("M7 start PPSA50010 - actual runtime pending X button");
  pad = OpenPad();
  ps5::demo::run(Draw,"Xbox360PS5 actual CPU runtime test");
}
