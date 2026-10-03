// SPDX-License-Identifier: MIT
// Native game entry: actual Xenia factories, KHR_display and native pad polling.
#include "xenia/emulator.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include "xbox360ps5/crash_report.hpp"
#include "xbox360ps5/display_surface.hpp"
#include "xbox360ps5/dualsense_input.hpp"
#include "xbox360ps5/game_patches.hpp"
#include "xbox360ps5/autotest.hpp"
#include "xbox360ps5/launcher.hpp"
#include "xbox360ps5/title_switch.hpp"
#include "xenia/kernel/xam/xam_module.h"
#include "xbox360ps5/perf.hpp"
#include "xbox360ps5/native_audio.hpp"
#include "third_party/imgui/imgui.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <unistd.h>
DECLARE_path(log_file);
DECLARE_int32(log_level);
extern "C" {
int sceUserServiceInitialize(const void*);
int sceUserServiceGetInitialUser(int32_t*);
int sceUserServiceGetLoginUserIdList(int32_t*);
int scePadInit();
int scePadOpen(int32_t, int32_t, int32_t, const void*);
int scePadReadState(int32_t, void*);
int scePadClose(int32_t);
int sceKernelDebugOutText(int, const char*);
int sceSystemServiceLoadExec(const char*, const char**);
int sceSystemServiceHideSplashScreen();
}
namespace xe { extern void (*log_mirror)(char, const char*, size_t); }
namespace {
// The frame counter drawn over a game when the setting asks for it.
class FpsOverlay final : public xe::ui::ImGuiDialog {
 public:
  FpsOverlay(xe::ui::ImGuiDrawer* drawer, const float& fps) : ImGuiDialog(drawer), fps_(fps) {}
 protected:
  void OnDraw(ImGuiIO& io) override {
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f FPS", fps_);
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const float scale = io.DisplaySize.y / 1080.0f;
    list->AddRectFilled(ImVec2(24 * scale, 24 * scale), ImVec2(150 * scale, 64 * scale), IM_COL32(0, 0, 0, 150), 8 * scale);
    list->AddText(ImGui::GetFont(), 28 * scale, ImVec2(36 * scale, 30 * scale), IM_COL32(120, 230, 140, 255), text);
  }
 private:
  const float& fps_;
};
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
  std::vector<uint8_t> TakeIcon() { return std::move(icon_); }
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
  void LoadAndApplyIcon(const void* buffer, size_t size, bool) override {
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    icon_.assign(bytes, bytes + (buffer ? size : 0));
  }
 private:
  uint32_t ui_buttons_ = 0;
  std::vector<uint8_t> icon_;
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
  xe::ui::ImmediateDrawer& immediate;
  xbox360ps5::Launcher& launcher;
  ~DetachPresentation() {
    launcher.SetDrawer(nullptr);  // Cover textures go before their drawer.
    drawer.SetPresenterAndImmediateDrawer(nullptr, nullptr);
    immediate.SetPresenter(nullptr);
    window.SetPresenter(nullptr);
  }
};
}
void KernelLogMirror(char prefix, const char* text, size_t size) {
  char buffer[512];
  const int length = std::snprintf(buffer, sizeof(buffer), "[X360] %c> %.*s\n", prefix,
                                   int(std::min<size_t>(size, 480)), text);
  if (length <= 0) return;
  // The kernel log is a small, slow ring: only warnings and errors go there.
  // The network stream carries everything.
  // A game can repeat one warning thousands of times a second (an unknown
  // register, say): past 30 lines in a second the kernel log gets no more, so
  // the game is not slowed down and a crash report is not pushed out of the ring.
  static std::atomic<int64_t> second{0};
  static std::atomic<int> lines{0};
  const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  if (second.exchange(now) != now) lines = 0;
  if ((prefix == '!' || prefix == 'w') && ++lines <= 30) sceKernelDebugOutText(0, buffer);
  xbox360ps5::NetLog(buffer, std::min<size_t>(size_t(length), sizeof(buffer) - 1));
}
int main(int argc, char** argv) {
  const std::filesystem::path storage = "/download0/xbox360ps5";
  std::error_code error;
  std::filesystem::create_directories(storage, error);
  if (error) { std::fprintf(stderr, "Writable storage unavailable: %s\n", error.message().c_str()); return 1; }
  xbox360ps5::InstallCrashReport("/download0/xbox360ps5/boot.log");
  using xbox360ps5::Stage;
  Stage("BOOT main entered");
  // Relative paths must resolve to "no such file" rather than a sandbox error.
  if (chdir("/download0/xbox360ps5")) Stage("BOOT chdir refused");
  xbox360ps5::ReportPlatformMemory();
  Stage(xbox360ps5::StartNetLog(9100) ? "BOOT log stream on TCP 9100" : "BOOT log stream unavailable");
  xe::log_mirror = KernelLogMirror;
  xbox360ps5::Settings settings;
  settings.Load();
  settings.Apply();
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
  // An explicit path starts that game at once; otherwise the library opens.
  if (!game.empty() && !std::filesystem::is_regular_file(game, error)) {
    XELOGE("Configured game {} does not exist; opening the library", game.string());
    game.clear();
  }
  XELOGI("GAME FILE {}", game.empty() ? "(library)" : game.string());
  Stage("BOOT window");
  NativeContext context;
  NativeWindow window(context);
  if (!window.Open()) return 3;
  Stage("BOOT imgui");
  xe::ui::ImGuiDrawer drawer(&window, 1);
  drawer.GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  // Text sized for a television; the emulator's own dialogs use it as well.
  const auto fonts = xbox360ps5::Launcher::LoadFonts(drawer.GetIO());
  if (fonts.f28) drawer.GetIO().FontDefault = fonts.f28;
  else { drawer.GetIO().FontGlobalScale = 2.5f; Stage("BOOT launcher font missing, using the built-in one"); }
  xbox360ps5::Launcher launcher(fonts, settings);
  Stage("BOOT pad");
  NativePad pad;
  int result = 0;
  bool restart = false;
  {
    Stage("BOOT emulator constructor");
    xe::Emulator emulator("", storage, storage / "content", storage / "cache");
    Stage("BOOT emulator setup");
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
    Stage("BOOT emulator setup returned");
    xbox360ps5::ReportPlatformMemory();
    if (status) { result = 4; }
    else {
      auto immediate = emulator.graphics_system()->provider()->CreateImmediateDrawer();
      if (!immediate || !emulator.graphics_system()->presenter()) {
        XELOGE("Native Vulkan presentation/UI initialization failed");
        return 6;
      }
      DetachPresentation detach{window, drawer, *immediate, launcher};
      window.SetPresenter(emulator.graphics_system()->presenter());
      // The drawer uploads its textures through the presenter.
      immediate->SetPresenter(emulator.graphics_system()->presenter());
      drawer.SetPresenterAndImmediateDrawer(emulator.graphics_system()->presenter(), immediate.get());
      launcher.SetDrawer(immediate.get());
      // The system keeps the title's start-up picture (sce_sys/pic0.png) on screen
      // until the title says it is ready to show its own.
      sceSystemServiceHideSplashScreen();
      emulator.on_exit.AddListener([&context] { context.RequestDeferredQuit(); });
      bool launched = false;
      xbox360ps5::GameEntry chosen;
      // An unattended compatibility run, when the title folder asks for one.
      xbox360ps5::AutoTest autotest;
      // A game asked the previous run to start another of its executables.
      xbox360ps5::PendingLaunch pending;
      const bool switched = game.empty() && xbox360ps5::TakePendingLaunch(pending);
      if (switched) {
        game = pending.game;
        const std::string extension = game.extension().string();
        chosen = {game.parent_path().filename().string(), game,
                  extension == ".xex" || extension == ".XEX" ? "XEX" : extension == ".iso" || extension == ".ISO" ? "ISO" : "GOD",
                  game.parent_path().string()};
        xbox360ps5::SetModuleOverride(pending.module);
        auto xam = emulator.kernel_state()->GetKernelModule<xe::kernel::xam::XamModule>("xam.xex");
        xam->loader_data().launch_data = pending.launch_data;
        xam->loader_data().launch_data_present = !pending.launch_data.empty();
        XELOGW("Title switch: starting {} in {} ({} bytes of launch data)", pending.module, game.string(),
               pending.launch_data.size());
      }
      if (game.empty()) {
        launcher.Scan();
        const auto paths = launcher.GamePaths();
        if (autotest.Begin(paths)) {
          game = paths[size_t(autotest.index)];
          chosen = {game.parent_path().filename().string(), game, "XEX", game.parent_path().string()};
        }
      }
      if (!game.empty()) chosen = {game.filename().string(), game, "XEX", game.parent_path().string()};
      while (!launched && !restart && !context.HasQuitFromUIThread()) {
        xbox360ps5::LauncherDialog* dialog = nullptr;
        if (game.empty()) {
          // The launcher: pick a game with the pad.
          Stage("BOOT launcher");
          launcher.Scan();
          dialog = new xbox360ps5::LauncherDialog(&drawer, launcher);
          uint32_t held = ~0u;  // Buttons already down when the screen opens do not count.
          auto repeat_at = std::chrono::steady_clock::now();
          while (!launcher.TakeLaunch(chosen) && !context.HasQuitFromUIThread()) {
            if (launcher.TakeRestart()) { restart = true; break; }
            const auto sample = pad.Read();
            launcher.SetPadConnected(sample.connected);
            uint32_t buttons = sample.connected ? sample.buttons : 0;
            if (sample.connected && sample.ly < 64) buttons |= 0x10;
            if (sample.connected && sample.ly > 192) buttons |= 0x40;
            if (sample.connected && sample.lx < 64) buttons |= 0x80;
            if (sample.connected && sample.lx > 192) buttons |= 0x20;
            uint32_t pressed = buttons & ~held;
            const auto now = std::chrono::steady_clock::now();
            // Holding a direction keeps moving.
            if (pressed & 0xF0) repeat_at = now + std::chrono::milliseconds(400);
            else if ((buttons & 0xF0) && now >= repeat_at) {
              pressed |= buttons & 0xF0;
              repeat_at = now + std::chrono::milliseconds(110);
            }
            held = buttons;
            using xbox360ps5::Key;
            const std::pair<uint32_t, Key> keys[] = {{0x10, Key::up}, {0x40, Key::down}, {0x80, Key::left},
              {0x20, Key::right}, {0x4000, Key::cross}, {0x2000, Key::circle}, {0x1000, Key::triangle},
              {0x8000, Key::square}, {0x400, Key::l1}, {0x800, Key::r1}};
            for (const auto& [button, key] : keys) if (pressed & button) launcher.Press(key);
            context.Tick(); window.Paint();
          }
          if (restart || context.HasQuitFromUIThread()) { dialog->Dismiss(); break; }
          game = chosen.path;
          // Show the loading screen before the launch blocks this thread.
          for (int frame = 0; frame < 3; ++frame) { context.Tick(); window.Paint(); }
        }
        Stage("BOOT launch");
        XELOGI("GAME FILE {}", game.string());
        window.TakeIcon();
        // A game that starts another executable mounts this one again (title_switch.hpp).
        xbox360ps5::SetLaunchedGame(game);
        status = emulator.LaunchPath(game);
        XELOGI("GAME LAUNCH {:08X}", status);
        Stage("BOOT launch returned");
        // After a switch the running executable is not the one the library lists.
        if (!status && !switched) launcher.RecordLaunch(chosen, emulator.title_id(), emulator.title_name(), window.TakeIcon(),
                                           xbox360ps5::LastModuleHash());
        launcher.SetLoading("");
        if (dialog) { dialog->Dismiss(); context.Tick(); window.Paint(); }
        if (!status) { launched = true; break; }
        if (!dialog) { result = 5; break; }
        char text[96];
        std::snprintf(text, sizeof(text), "Não foi possível iniciar este jogo (erro %08X).", unsigned(status));
        launcher.SetMessage(text);
        game.clear();
      }
      if (launched) {
        auto combo_since = std::chrono::steady_clock::time_point::max();
        // The game's frame rate: shown when asked for, and in the log every 30 seconds.
        float fps = 0.0f;
        if (settings.show_fps) new FpsOverlay(&drawer, fps);
        auto sample_at = std::chrono::steady_clock::now();
        uint64_t sampled_frames = xbox360ps5::guest_frames.load();
        int samples = 0;
        float fps_sum = 0.0f, fps_low = 1e9f;
        const auto game_started = std::chrono::steady_clock::now();
        double next_shot = 5;
        while (!context.HasQuitFromUIThread()) {
          auto sample = pad.Read();
          if (autotest.active) {
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - game_started).count();
            sample.connected = true;
            sample.buttons |= autotest.Buttons(elapsed);
            if (elapsed >= next_shot) {
              next_shot += 10;
              xe::ui::RawImage image;
              if (emulator.graphics_system()->presenter()->CaptureGuestOutput(image))
                xbox360ps5::AutoTest::SendShot(image, autotest.index, std::to_string(int(elapsed)) + "s");
              XELOGW("AUTOTEST {} at {:.0f} s: {:.1f} FPS, {} frames", autotest.index + 1, elapsed, fps,
                     xbox360ps5::guest_frames.load());
            }
            if (elapsed >= autotest.seconds) {
              xbox360ps5::DrainNetLog();
              restart = true;
              break;
            }
          }
          input->Submit(sample);
          window.SubmitUiPad(sample);
          context.Tick(); window.Paint();
          if (std::chrono::steady_clock::now() - sample_at >= std::chrono::seconds(1)) {
            const auto at = std::chrono::steady_clock::now();
            const uint64_t frames = xbox360ps5::guest_frames.load();
            fps = float(frames - sampled_frames) / std::chrono::duration<float>(at - sample_at).count();
            sample_at = at; sampled_frames = frames;
            fps_sum += fps; fps_low = std::min(fps_low, fps);
            if (++samples == 30) {
              XELOGW("Desempenho: media {:.1f} quadros/s, minimo {:.0f} nos ultimos 30 s", fps_sum / 30, fps_low);
              samples = 0; fps_sum = 0.0f; fps_low = 1e9f;
            }
          }
          // OPTIONS and the touchpad held for two seconds: back to the launcher.
          const bool combo = sample.connected && (sample.buttons & 0x100008) == 0x100008;
          const auto now = std::chrono::steady_clock::now();
          if (!combo) combo_since = std::chrono::steady_clock::time_point::max();
          else if (combo_since == std::chrono::steady_clock::time_point::max()) combo_since = now;
          else if (now - combo_since > std::chrono::seconds(2)) { restart = true; break; }
        }
        if (!restart && emulator.is_title_open()) emulator.TerminateTitle();
      }
      if (!restart) context.ExecutePendingFunctionsFromUIThread();
    }
    if (restart) {
      // A fresh process is the reliable way back to the launcher: the system
      // ends this one and runs the title's executable again.
      XELOGI("ENGINE RESTART");
      Stage("BOOT restarting the title");
      xe::ShutdownLogging();
      const int refused = sceSystemServiceLoadExec("/app0/eboot.bin", nullptr);
      char text[64];
      std::snprintf(text, sizeof(text), "BOOT restart refused %08x", unsigned(refused));
      Stage(text);
    }
  }
  XELOGI("ENGINE EXIT {}", result);
  xe::ShutdownLogging();
  return result;
}
// main returned: ask the system to end the title. The C library's exit() is
// answered with a signal in a native title, which the system reports as a crash.
extern "C" void catchReturnFromMain(int status) {
  char text[64];
  std::snprintf(text, sizeof(text), "BOOT main returned %d", status);
  xbox360ps5::Stage(text);
  sceSystemServiceLoadExec("exit", nullptr);
  for (;;) usleep(100000);
}
