// SPDX-License-Identifier: MIT
#include "xenia/emulator.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xbox360ps5/game_patches.hpp"
#include "xbox360ps5/native_audio.hpp"
#include "xbox360ps5/dualsense_input.hpp"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/kernel/user_module.h"
#include "xenia/base/cvar.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xbox360ps5/title_switch.hpp"
#include "xbox360ps5/covers.hpp"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "xenia/ui/presenter.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "third_party/stb/stb_image_write.h"
#include <filesystem>
#include <vector>
#include <atomic>
extern std::atomic<int> xbox360ps5_draw_log;  // vulkan_command_processor.cc overlay.
namespace config {
void ReadGameConfig(const std::filesystem::path& file_path);  // config.cc, not in config.h.
}
DECLARE_int32(log_level);
DECLARE_bool(gpu_allow_invalid_fetch_constants);
DECLARE_bool(flush_log);
DECLARE_bool(log_high_frequency_kernel_calls);
namespace {
// A window with no surface: the game runs and the renderer works off-screen.
class HeadlessContext final : public xe::ui::WindowedAppContext {
 public:
  void Tick() { ExecutePendingFunctionsFromUIThread(); }
 protected:
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};
class HeadlessWindow final : public xe::ui::Window {
 public:
  explicit HeadlessWindow(HeadlessContext& context) : Window(context, "Xbox360PS5 host run", 1280, 720) {}
  ~HeadlessWindow() override { EnterDestructor(); }
 protected:
  bool OpenImpl() override {
    WindowDestructionReceiver receiver(this);
    OnActualSizeUpdate(1280, 720, receiver);
    return true;
  }
  void RequestCloseImpl() override {}
  std::unique_ptr<xe::ui::Surface> CreateSurfaceImpl(xe::ui::Surface::TypeFlags) override { return nullptr; }
  void RequestPaintImpl() override {}
};
}
int main(int argc, char** argv) {
  // --run-xex executes the game headless for a number of seconds: a host
  // reference trace to compare with what the console does.
  // --covers <game>...: the title id the launcher reads from each game (.xex or
  // .iso), then the download of their covers, as the launcher does it.
  if (argc >= 3 && std::string_view(argv[1]) == "--covers") {
    std::vector<std::string> ids;
    for (int n = 2; n < argc; ++n) {
      const std::filesystem::path game = argv[n];
      const std::string id = game.extension() == ".iso" ? xbox360ps5::ReadIsoTitleId(game) : xbox360ps5::ReadXexTitleId(game);
      std::printf("TITLE ID %s %s\n", id.empty() ? "--------" : id.c_str(), game.string().c_str());
      if (!id.empty()) ids.push_back(id);
    }
    xbox360ps5::CoverDownloader downloader;
    downloader.Start(ids);
    while (downloader.Busy()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::printf("COVERS %s\n", downloader.Status().c_str());
    for (const auto& id : ids) {
      std::error_code error;
      const auto file = xbox360ps5::CoverFile(id);
      std::printf("COVER %s %lld bytes\n", file.string().c_str(),
                  static_cast<long long>(std::filesystem::exists(file, error) ? std::filesystem::file_size(file, error) : 0));
    }
    return 0;
  }
  // --check-patches reads every patch file in a folder with the title's parser.
  if (argc == 3 && std::string_view(argv[1]) == "--check-patches") {
    size_t files = 0, unreadable = 0, patches = 0, writes = 0, empty = 0;
    for (const auto& entry : std::filesystem::directory_iterator(argv[2])) {
      xbox360ps5::PatchFile file;
      ++files;
      const std::string name = entry.path().filename().string();
      if (!xbox360ps5::ReadPatchFile(entry.path().string(), file)) { ++unreadable; std::printf("UNREADABLE %s\n", name.c_str()); continue; }
      if (file.hashes.empty()) std::printf("NO HASH %s\n", name.c_str());
      for (const auto& patch : file.patches) {
        ++patches; writes += patch.writes.size();
        if (patch.writes.empty() || patch.name.empty()) { ++empty; std::printf("EMPTY PATCH %s: %s\n", name.c_str(), patch.name.c_str()); }
      }
    }
    std::printf("PATCH FILES %zu unreadable %zu patches %zu writes %zu empty %zu\n", files, unreadable, patches, writes, empty);
    return unreadable || empty ? 7 : 0;
  }
  // An optional fourth argument is a folder for a screenshot of the game every five seconds.
  const bool run = (argc == 4 || argc == 5) && std::string_view(argv[1]) == "--run-xex";
  if (!run && argc != 1 && (argc != 3 || std::string_view(argv[1]) != "--load-xex")) return 3;
  if (run) cvars::log_level = 3;
  cvars::gpu_allow_invalid_fetch_constants = true;  // As the title sets it.
  // The trace stays in memory until the end: writing each line out would change the
  // game's timing, and timing is what most stalls depend on.
  if (run) { cvars::flush_log = false; std::setvbuf(stdout, nullptr, _IOFBF, size_t(512) << 20); }
  // XBOX360PS5_TRACE_ALL=1 also logs the calls games make constantly (waits, reads).
  if (run && std::getenv("XBOX360PS5_TRACE_ALL")) cvars::log_high_frequency_kernel_calls = true;
  // XBOX360PS5_CONFIG=<file>: a game config (qualified keys such as GPU.vsync = false)
  // applied before the emulator starts, for trying settings on one game.
  if (const char* file = std::getenv("XBOX360PS5_CONFIG"); run && file) config::ReadGameConfig(file);
  const auto root = std::filesystem::temp_directory_path() / "xbox360ps5-engine-lifecycle";
  std::filesystem::create_directories(root);
  {
    xe::Emulator emulator("", root, root / "content", root / "cache");
    HeadlessContext context;
    xbox360ps5::DualSenseInput* pad_driver = nullptr;
    HeadlessWindow window(context);
    if (run && !window.Open()) return 6;
    const auto status = emulator.Setup(run ? &window : nullptr, nullptr, true,
        [](xe::cpu::Processor* processor) -> std::unique_ptr<xe::apu::AudioSystem> {
          return std::make_unique<xbox360ps5::NativeAudioSystem>(processor, 0xff);
        },
        []() -> std::unique_ptr<xe::gpu::GraphicsSystem> {
          return std::make_unique<xe::gpu::vulkan::VulkanGraphicsSystem>();
        },
        [&pad_driver](xe::ui::Window*) {
          std::vector<std::unique_ptr<xe::hid::InputDriver>> drivers;
          auto driver = std::make_unique<xbox360ps5::DualSenseInput>();
          pad_driver = driver.get();
          drivers.push_back(std::move(driver));
          return drivers;
        });
    std::printf("ACTUAL ENGINE SETUP %08x\n", unsigned(status));
    if (status != 0) return 1;
    if (!emulator.kernel_state() || !emulator.audio_system() ||
        !emulator.graphics_system() || !emulator.processor() || !emulator.file_system()) return 2;
    std::puts("KERNEL CPU VFS XMA VULKAN INITIALIZED (no game executed, no display/audio port claimed)");
    if (run) {
      // XBOX360PS5_RESUME: run the title switch the previous run asked for, as
      // the console does after its restart (title_switch.hpp).
      std::filesystem::path game = argv[2];
      xbox360ps5::PendingLaunch pending;
      if (std::getenv("XBOX360PS5_RESUME") && xbox360ps5::TakePendingLaunch(pending)) {
        game = pending.game;
        xbox360ps5::SetModuleOverride(pending.module);
        auto xam = emulator.kernel_state()->GetKernelModule<xe::kernel::xam::XamModule>("xam.xex");
        xam->loader_data().launch_data = pending.launch_data;
        xam->loader_data().launch_data_present = !pending.launch_data.empty();
        std::printf("TITLE SWITCH %s with %zu bytes of launch data\n", pending.module.c_str(), pending.launch_data.size());
      }
      xbox360ps5::SetLaunchedGame(game);
      const auto launched = emulator.LaunchPath(game);
      std::printf("HOST GAME LAUNCH %08x\n", unsigned(launched));
      std::fflush(stdout);
      auto next_shot = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      int shot = 0;
      const auto started = std::chrono::steady_clock::now();
      const bool autopress = std::getenv("XBOX360PS5_NO_AUTOPRESS") == nullptr;
      const bool trace_tail = std::getenv("XBOX360PS5_TRACE_TAIL") != nullptr;
      const bool press_right = std::getenv("XBOX360PS5_AUTOPRESS_RIGHT") != nullptr;
      // XBOX360PS5_DRAW_LOG_AT=<seconds>: every draw of one frame at that time in the log.
      const char* draw_log_at = std::getenv("XBOX360PS5_DRAW_LOG_AT");
      const auto draw_log_time = started + std::chrono::seconds(draw_log_at ? std::atoi(draw_log_at) : 1000000);
      bool draw_log_armed = draw_log_at != nullptr;
      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(std::atoi(argv[3]));
      while (std::chrono::steady_clock::now() < end) {
        context.Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (draw_log_armed && std::chrono::steady_clock::now() >= draw_log_time) {
          draw_log_armed = false;
          xbox360ps5_draw_log = 1;
        }
        if (pad_driver) {
          // Every 4 s: START, then A two seconds later, each held for a tenth of a second.
          const auto phase = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - started).count() % 4000;
          xbox360ps5::PadSample sample;
          sample.connected = true;
          if (autopress && phase < 100) sample.buttons = 0x08;
          if (autopress && phase >= 2000 && phase < 2100) sample.buttons = 0x4000;
          // XBOX360PS5_AUTOPRESS_RIGHT=1: also the D-pad right a second after START,
          // for menus that need a choice before A does anything.
          if (autopress && press_right && phase >= 1000 && phase < 1100) sample.buttons = 0x20;
          pad_driver->Submit(sample);
        }
        // XBOX360PS5_TRACE_TAIL=1: trace every call for the last two seconds only, to see
        // what each thread is doing once the game has settled (or stalled).
        if (trace_tail && std::chrono::steady_clock::now() >= end - std::chrono::seconds(2))
          cvars::log_high_frequency_kernel_calls = true;
        if (argc == 5 && std::chrono::steady_clock::now() >= next_shot) {
          next_shot += std::chrono::seconds(5);
          xe::ui::RawImage image;
          if (emulator.graphics_system()->presenter()->CaptureGuestOutput(image) && image.width) {
            for (size_t at = 3; at < image.data.size(); at += 4) image.data[at] = 255;
            char name[512];
            std::snprintf(name, sizeof(name), "%s/shot-%03d.png", argv[4], shot++);
            stbi_write_png(name, int(image.width), int(image.height), 4, image.data.data(), int(image.stride));
          }
        }
      }
      std::fflush(stdout);
      std::_Exit(0);
    }
    if (argc == 3) {
      const std::filesystem::path game = argv[2];
      auto device = std::make_unique<xe::vfs::HostPathDevice>("\\Device\\GameInspection", game.parent_path(), true);
      if (!device->Initialize() || !emulator.file_system()->RegisterDevice(std::move(device))) return 4;
      emulator.file_system()->RegisterSymbolicLink("game:", "\\Device\\GameInspection");
      // Disable DLL entry execution: this diagnostic only resolves the real
      // image and imports in guest memory. It never runs the supplied game.
      auto module = emulator.kernel_state()->LoadUserModule("game:\\" + game.filename().string(), false);
      if (!module || !module->entry_point()) { std::puts("ACTUAL GAME MODULE LOAD FAILED"); return 5; }
      std::printf("ACTUAL GAME MODULE LOADED ENTRY %08x (read-only files; not executed)\n", module->entry_point());
      emulator.kernel_state()->UnloadUserModule(module, false);
    }
  }
  std::puts("ACTUAL ENGINE SHUTDOWN COMPLETE");
  return 0;
}
