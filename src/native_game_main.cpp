// SPDX-License-Identifier: MIT
// Native game entry: actual Xenia factories, KHR_display and native pad polling.
#include "xenia/emulator.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include "xbox360ps5/display_surface.hpp"
#include "xbox360ps5/dualsense_input.hpp"
#include "xbox360ps5/native_audio.hpp"
#include "third_party/imgui/imgui.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <cstdio>
DECLARE_path(log_file);
extern "C" {
int sceUserServiceInitialize(const void*);
int sceUserServiceGetInitialUser(int32_t*);
int sceUserServiceGetLoginUserIdList(int32_t*);
int scePadInit();
int scePadOpen(int32_t, int32_t, int32_t, const void*);
int scePadReadState(int32_t, void*);
int scePadClose(int32_t);
}
namespace {
class NativeContext final : public xe::ui::WindowedAppContext {
 public:
  void Tick() {
    ExecutePendingFunctionsFromUIThread();
    std::unique_lock lock(mutex_);
    wake_.wait_for(lock, std::chrono::milliseconds(2));
  }
 protected:
  void NotifyUILoopOfPendingFunctions() override { wake_.notify_all(); }
  void PlatformQuitFromUIThread() override { wake_.notify_all(); }
 private:
  std::mutex mutex_;
  std::condition_variable wake_;
};
class NativeWindow final : public xe::ui::Window {
 public:
  explicit NativeWindow(NativeContext& context) : Window(context, "Xbox360PS5", 1920, 1080) {}
  ~NativeWindow() override { EnterDestructor(); }
  void Paint() { OnPaint(); }
  void SubmitUiPad(const xbox360ps5::PadSample& pad) {
    const uint32_t current = pad.connected ? pad.buttons : 0;
    const uint32_t changed = current ^ ui_buttons_;
    using Key = xe::ui::VirtualKey;
    const std::pair<uint32_t, Key> keys[] = {{0x10, Key::kUp}, {0x20, Key::kRight},
      {0x40, Key::kDown}, {0x80, Key::kLeft}, {0x4000, Key::kReturn},
      {0x2000, Key::kEscape}, {0x8000, Key::kTab}};
    WindowDestructionReceiver receiver(this);
    for (const auto& [button, key] : keys) {
      if (!(changed & button)) continue;
      xe::ui::KeyEvent event(this, key, 1, bool(ui_buttons_ & button), false, false, false, false);
      if (current & button) OnKeyDown(event, receiver); else OnKeyUp(event, receiver);
    }
    ui_buttons_ = current;
  }
 protected:
  bool OpenImpl() override {
    WindowDestructionReceiver receiver(this);
    OnActualSizeUpdate(1920, 1080, receiver);
    OnFocusUpdate(true, receiver);
    OnDesiredFullscreenUpdate(true);
    return true;
  }
  void RequestCloseImpl() override {
    WindowDestructionReceiver receiver(this);
    OnBeforeClose(receiver);
    OnAfterClose();
    app_context().RequestDeferredQuit();
  }
  std::unique_ptr<xe::ui::Surface> CreateSurfaceImpl(xe::ui::Surface::TypeFlags allowed) override {
    if (!(allowed & xe::ui::Surface::kTypeFlag_KhrDisplay)) return nullptr;
    return std::make_unique<xbox360ps5::DisplaySurface>(1920, 1080);
  }
  void RequestPaintImpl() override {}  // The native event loop calls Paint.
 private:
  uint32_t ui_buttons_ = 0;
};
class NativePad {
 public:
  NativePad() {
    (void)sceUserServiceInitialize(nullptr);
    if (sceUserServiceGetInitialUser(&user) < 0) {
      std::array<int32_t, 4> users{-1, -1, -1, -1};
      if (sceUserServiceGetLoginUserIdList(users.data()) >= 0) {
        for (auto id : users) if (id >= 0) { user = id; break; }
      }
    }
    if (scePadInit() >= 0) handle = scePadOpen(user, 0, 0, nullptr);
    std::printf("NATIVE PAD user=%08x handle=%08x\n", unsigned(user), unsigned(handle));
  }
  ~NativePad() { if (handle >= 0) scePadClose(handle); }
  xbox360ps5::PadSample Read() {
    xbox360ps5::PadSample state;
    alignas(16) std::array<uint8_t, 256> bytes{};
    if (handle < 0 || scePadReadState(handle, bytes.data()) < 0) return state;
    state.buttons = uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 |
        uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
    state.lx = bytes[4]; state.ly = bytes[5]; state.rx = bytes[6]; state.ry = bytes[7];
    state.l2 = bytes[8]; state.r2 = bytes[9]; state.connected = bytes[76] != 0;
    return state;
  }
  int32_t user = 0xff;
 private:
  int32_t handle = -1;
};
struct DetachPresentation {
  NativeWindow& window;
  xe::ui::ImGuiDrawer& drawer;
  ~DetachPresentation() {
    drawer.SetPresenterAndImmediateDrawer(nullptr, nullptr);
    window.SetPresenter(nullptr);
  }
};
}
int main(int argc, char** argv) {
  const std::filesystem::path storage = "/download0/xbox360ps5";
  std::error_code error;
  std::filesystem::create_directories(storage, error);
  if (error) { std::fprintf(stderr, "Writable storage unavailable: %s\n", error.message().c_str()); return 1; }
  cvars::log_file = storage / "engine.log";
  xe::InitializeLogging("Xbox360PS5 integrated game entry (experimental)");
  std::filesystem::path game;
  if (argc > 1 && argv[1]) game = argv[1];
  else {
    std::ifstream input(storage / "game.txt");
    std::string line;
    if (std::getline(input, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); game = line; }
  }
  if (game.empty()) {
    std::ifstream input("/app0/assets/game.txt");
    std::string line;
    if (std::getline(input, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); game = line; }
  }
  if (game.empty()) game = "/app0/assets/roms/default.xex";
  if (game.empty() || !std::filesystem::is_regular_file(game, error)) {
    XELOGE("Provide an existing default.xex path as argv[1] or in /download0/xbox360ps5/game.txt");
    xe::ShutdownLogging();
    return 2;
  }
  NativeContext context;
  NativeWindow window(context);
  if (!window.Open()) return 3;
  xe::ui::ImGuiDrawer drawer(&window, 1);
  drawer.GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  NativePad pad;
  int result = 0;
  {
    xe::Emulator emulator("", storage, storage / "content", storage / "cache");
    xbox360ps5::DualSenseInput* input = nullptr;
    auto status = emulator.Setup(&window, &drawer, true,
      [&pad](xe::cpu::Processor* cpu) -> std::unique_ptr<xe::apu::AudioSystem> {
        return std::make_unique<xbox360ps5::NativeAudioSystem>(cpu, pad.user);
      },
      []() -> std::unique_ptr<xe::gpu::GraphicsSystem> {
        return std::make_unique<xe::gpu::vulkan::VulkanGraphicsSystem>();
      },
      [&input](xe::ui::Window*) {
        std::vector<std::unique_ptr<xe::hid::InputDriver>> drivers;
        auto driver = std::make_unique<xbox360ps5::DualSenseInput>();
        input = driver.get(); drivers.push_back(std::move(driver)); return drivers;
      });
    XELOGI("ENGINE SETUP {:08X}", status);
    if (status) { result = 4; }
    else {
      auto immediate = emulator.graphics_system()->provider()->CreateImmediateDrawer();
      if (!immediate || !emulator.graphics_system()->presenter()) {
        XELOGE("Native Vulkan presentation/UI initialization failed");
        return 6;
      }
      DetachPresentation detach{window, drawer};
      window.SetPresenter(emulator.graphics_system()->presenter());
      drawer.SetPresenterAndImmediateDrawer(emulator.graphics_system()->presenter(), immediate.get());
      emulator.on_exit.AddListener([&context] { context.RequestDeferredQuit(); });
      status = emulator.LaunchXexFile(game);
      XELOGI("GAME LAUNCH {:08X}", status);
      if (status) result = 5;
      else {
        while (!context.HasQuitFromUIThread()) {
          const auto sample = pad.Read();
          input->Submit(sample);
          window.SubmitUiPad(sample);
          context.Tick(); window.Paint();
          if (emulator.TitleRequested()) emulator.LaunchNextTitle();
        }
        if (emulator.is_title_open()) emulator.TerminateTitle();
      }
      context.ExecutePendingFunctionsFromUIThread();
    }
  }
  XELOGI("ENGINE EXIT {}", result);
  xe::ShutdownLogging();
  return result;
}
