// SPDX-License-Identifier: MIT
// Development log stream. The console's kernel log is a 128 KiB ring and far
// too small for a kernel-call trace, so the title also serves its log on a TCP
// port: lines are buffered from the first one and sent to the one client that
// connects (tools/console.py netlog).
#include "xbox360ps5/crash_report.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <pthread.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
namespace xbox360ps5 {
namespace {
constexpr size_t kMostBuffered = 32u << 20;
std::mutex guard;
std::string pending;
bool dropped = false;
int listener = -1;

bool SendAll(int client, const char* data, size_t size) {
  while (size) {
    const ssize_t sent = send(client, data, size, 0);
    if (sent <= 0) return false;
    data += sent; size -= size_t(sent);
  }
  return true;
}
void* Serve(void*) {
  for (;;) {
    const int client = accept(listener, nullptr, nullptr);
    if (client < 0) { usleep(200000); continue; }
    for (;;) {
      std::string batch;
      {
        std::lock_guard lock(guard);
        batch.swap(pending);
      }
      if (batch.empty()) { usleep(10000); continue; }
      if (!SendAll(client, batch.data(), batch.size())) break;
    }
    close(client);
  }
  return nullptr;
}
}
bool StartNetLog(unsigned short port) {
  listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) return false;
  const int enable = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  pthread_t thread;
  if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
      listen(listener, 1) || pthread_create(&thread, nullptr, Serve, nullptr)) {
    close(listener); listener = -1;
    return false;
  }
  pthread_detach(thread);
  return true;
}
void NetLog(const char* text, size_t size) {
  if (listener < 0) return;
  std::lock_guard lock(guard);
  if (pending.size() + size > kMostBuffered) {
    if (!dropped) pending += "[X360] NETLOG buffer full, lines dropped\n";
    dropped = true;
    return;
  }
  dropped = false;
  pending.append(text, size);
}
void DrainNetLog() {
  // Called from the crash report: give the sender thread time to empty the buffer.
  for (int n = 0; n < 100 && listener >= 0; ++n) {
    if (guard.try_lock()) {
      const bool empty = pending.empty();
      guard.unlock();
      if (empty) break;
    }
    usleep(10000);
  }
  usleep(50000);
}
}
