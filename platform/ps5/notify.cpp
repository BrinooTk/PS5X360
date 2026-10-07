// SPDX-License-Identifier: MIT
#include "xbox360ps5/notify.hpp"
#include "xenia/base/logging.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
extern "C" {
int sceKernelSendNotificationRequest(int, void*, size_t, int);
int sceKernelLoadStartModule(const char* name, size_t args, const void* argp, uint32_t flags, void* option, int* result);
int sceKernelDlsym(int handle, const char* symbol, void** address);
}
namespace xbox360ps5 {
namespace {
using RichSend = int (*)(int user_id, bool logged, const char* payload);
std::mutex mutex;
std::condition_variable wake;
std::deque<Toast> queue;
std::atomic<bool> started{false};
int user = 0;
// Text inside a JSON string.
std::string Escaped(const std::string& text) {
  std::string out;
  for (const unsigned char c : text) {
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c == '\n') out += "\\n";
    else if (c < 0x20) out += ' ';
    else out += char(c);
  }
  return out;
}
// The system library's entry, looked up once. Null when the console refuses.
RichSend FindRichSend() {
  static bool tried = false;
  static RichSend send = nullptr;
  if (tried) return send;
  tried = true;
  int handle = -1;
  for (const char* name : {"libSceNotification.sprx", "/system/common/lib/libSceNotification.sprx"}) {
    handle = sceKernelLoadStartModule(name, 0, nullptr, 0, nullptr, nullptr);
    XELOGW("Notify: loading {} gave {:08X}", name, unsigned(handle));
    if (handle >= 0) break;
  }
  if (handle < 0) return nullptr;
  void* address = nullptr;
  const int found = sceKernelDlsym(handle, "sceNotificationSend", &address);
  XELOGW("Notify: sceNotificationSend lookup gave {:08X}", unsigned(found));
  if (found >= 0 && address) send = reinterpret_cast<RichSend>(address);
  return send;
}
bool SendRich(const Toast& toast) {
  const RichSend send = FindRichSend();
  if (!send) return false;
  static std::atomic<unsigned> serial{1};
  char when[40];
  const std::time_t now = std::time(nullptr);
  std::tm utc{};
  gmtime_r(&now, &utc);
  std::strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%S.000Z", &utc);
  const std::string icon = toast.icon_path.empty()
      ? "{\"type\":\"Predefined\",\"parameters\":{\"icon\":\"trophy\"}}"
      : "{\"type\":\"Url\",\"parameters\":{\"url\":\"" + Escaped(toast.icon_path) + "\"}}";
  std::string json = "{\"rawData\":{\"viewTemplateType\":\"InteractiveToastTemplateB\",\"channelType\":\"ServiceFeedback\","
      "\"useCaseId\":\"IDC\",\"toastOverwriteType\":\"No\",\"isImmediate\":true,\"priority\":100,";
  if (toast.trophy_sound) json += "\"soundEffect\":\"psfx_trophy_toast\",";
  json += "\"viewData\":{\"icon\":" + icon + ",\"message\":{\"body\":\"" + Escaped(toast.title) +
          "\"},\"subMessage\":{\"body\":\"" + Escaped(toast.text) + "\"}}},\"createdDateTime\":\"" + when +
          "\",\"localNotificationId\":\"" + std::to_string(588000000u + serial.fetch_add(1)) + "\"}";
  const int result = send(user, true, json.c_str());
  XELOGW("Notify: system toast '{}' gave {:08X}", toast.title, unsigned(result));
  return result >= 0;
}
void SendPlain(const Toast& toast) {
  unsigned char request[3120] = {0};
  for (int i = 0; i < 4; ++i) request[0x10 + i] = 0xff;
  std::snprintf(reinterpret_cast<char*>(request) + 45, 1024, "%s\n%s", toast.title.c_str(), toast.text.c_str());
  const int result = sceKernelSendNotificationRequest(0, request, sizeof(request), 0);
  XELOGW("Notify: plain message '{}' gave {:08X}", toast.title, unsigned(result));
}
void Worker() {
  for (;;) {
    Toast toast;
    {
      std::unique_lock<std::mutex> lock(mutex);
      wake.wait(lock, [] { return !queue.empty(); });
      toast = std::move(queue.front());
      queue.pop_front();
    }
    if (!SendRich(toast)) SendPlain(toast);
  }
}
}
void StartNotifier(int user_id) {
  user = user_id;
  if (started.exchange(true)) return;
  std::thread(Worker).detach();
}
void Notify(Toast toast) {
  if (!started.load()) return;
  { std::lock_guard<std::mutex> lock(mutex); if (queue.size() < 16) queue.push_back(std::move(toast)); }
  wake.notify_one();
}
std::string SaveAchievementIcon(uint32_t title_id, uint32_t achievement_id, std::span<const uint8_t> png) {
  if (png.size() < 8 || std::memcmp(png.data(), "\x89PNG", 4)) return {};
  // The installation folder by the path the whole console knows it by.
  const std::filesystem::path folder = "/data/homebrew/PPSA50011/cache/achievements";
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  char name[40];
  std::snprintf(name, sizeof(name), "%08X-%u.png", title_id, achievement_id);
  const auto path = folder / name;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
  out.flush();
  return out.good() ? path.string() : std::string();
}
}
