// SPDX-License-Identifier: MIT
// Runs a game on the Xenia Canary core, headless, on the PC: the reference
// for what the console does with the same core.
//   xbox360ps5-runner <game: .xex, .iso or package> <seconds> [screenshot folder]
// A virtual controller presses START and then A every four seconds (to get
// through menus); a screenshot of the guest output is taken every five.
// Environment: XBOX360PS5_NO_AUTOPRESS, XBOX360PS5_AUTOPRESS_RIGHT (also the
// D-pad right, for menus that need a choice), XBOX360PS5_CONFIG=<file> (a game
// config with qualified keys, e.g. GPU.vsync = false, applied before start).
#include "xbox360ps5/dualsense_input.hpp"
#include "xbox360ps5/canary_audio.hpp"
#include "xbox360ps5/gpu_upload_check.hpp"
#include "xbox360ps5/utility_cache.hpp"
#include "xbox360ps5/game_patches.hpp"
#include "xbox360ps5/content_header_check.hpp"
#include "xbox360ps5/patch_selection_check.hpp"
#include "xbox360ps5/utility_cache_check.hpp"
#include "xbox360ps5/module_path_check.hpp"
#include "xbox360ps5/file_open_check.hpp"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_state.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "third_party/stb/stb_image_write.h"
namespace config {
void ReadGameConfig(const std::filesystem::path& file_path);  // config.cc, not in config.h.
}
DECLARE_int32(log_level);
DECLARE_bool(flush_log);
DECLARE_bool(headless);
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
  explicit HeadlessWindow(HeadlessContext& context) : Window(context, "Xbox360PS5 runner", 1280, 720) {}
  ~HeadlessWindow() override { EnterDestructor(); }
 protected:
  bool OpenImpl() override {
    WindowDestructionReceiver receiver(this);
    OnActualSizeUpdate(1280, 720, WindowResizeAction::kManual, receiver);
    return true;
  }
  void RequestCloseImpl() override {}
  std::unique_ptr<xe::ui::Surface> CreateSurfaceImpl(xe::ui::Surface::TypeFlags) override { return nullptr; }
  void RequestPaintImpl() override {}
};
}

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--check-module-paths") == 0) {
    return xbox360ps5::CheckModulePaths();
  }
  if (argc == 2 && std::strcmp(argv[1], "--check-file-opening") == 0) {
    xe::InitializeLogging("xbox360ps5-file-check");
    return xbox360ps5::CheckFileOpening();
  }
  if (argc == 2 && std::strcmp(argv[1], "--check-utility-cache") == 0) {
    xe::InitializeLogging("xbox360ps5-cache-check");
    return xbox360ps5::CheckUtilityCache();
  }
  if (argc == 2 && std::strcmp(argv[1], "--check-upload-pages") == 0) {
    xe::InitializeLogging("xbox360ps5-upload-check");
    return xbox360ps5::CheckGpuUploadPages();
  }
  if (argc == 2 && std::strcmp(argv[1], "--check-content-headers") == 0) {
    xe::InitializeLogging("xbox360ps5-header-check");
    return xbox360ps5::CheckContentHeaders();
  }
  if (argc == 2 && std::strcmp(argv[1], "--check-patch-selection") == 0) { return xbox360ps5::CheckPatchSelection(); }
  if (argc != 3 && argc != 4) {
    std::fprintf(stderr, "usage: %s <game> <seconds> [screenshot folder]\n", argv[0]);
    return 3;
  }
  // XBOX360PS5_LOG_LEVEL=3: every kernel call; XBOX360PS5_TRACE_ALL=1: also the
  // ones games make constantly (waits, reads).
  cvars::log_level = std::getenv("XBOX360PS5_LOG_LEVEL") ? std::atoi(std::getenv("XBOX360PS5_LOG_LEVEL")) : 2;
  if (std::getenv("XBOX360PS5_TRACE_ALL")) cvars::log_high_frequency_kernel_calls = true;
  cvars::flush_log = false;
  // Nobody is there to answer the kernel's dialogs (sign-in, messages): they
  // answer themselves, and input reaches the game.
  cvars::headless = true;
  if (const char* file = std::getenv("XBOX360PS5_CONFIG")) config::ReadGameConfig(file);
  // Canary's log is a ring buffer drained by its own thread: without this the
  // first message waits forever.
  xe::InitializeLogging("xbox360ps5-runner");
  const auto root = std::filesystem::temp_directory_path() / "xbox360ps5-runner";
  std::filesystem::create_directories(root);
  xe::Emulator emulator("", root, root / "content", root / "cache");
  HeadlessContext context;
  HeadlessWindow window(context);
  if (!window.Open()) return 6;
  xbox360ps5::DualSenseInput* pad = nullptr;
  // Canary's kernel draws its notices through an ImGui drawer; it has no
  // presenter here, so nothing is drawn.
  xe::ui::ImGuiDrawer drawer(&window, 1);
  const auto status = emulator.Setup(&window, &drawer, true,
      [](xe::cpu::Processor* processor) -> std::unique_ptr<xe::apu::AudioSystem> {
        return std::make_unique<xbox360ps5::CanaryAudioSystem>(processor);
      },
      []() -> std::unique_ptr<xe::gpu::GraphicsSystem> {
        return std::make_unique<xe::gpu::vulkan::VulkanGraphicsSystem>();
      },
      [&pad](xe::ui::Window*) {
        std::vector<std::unique_ptr<xe::hid::InputDriver>> drivers;
        auto driver = std::make_unique<xbox360ps5::DualSenseInput>();
        pad = driver.get();
        drivers.push_back(std::move(driver));
        return drivers;
      });
  std::printf("RUNNER SETUP %08x\n", unsigned(status));
  if (status) return 1;
  if (!xbox360ps5::MountUtilityCache(*emulator.file_system(), root)) return 7;
  // Games give the controller to a signed-in profile: make one the first time.
  auto profiles = emulator.kernel_state()->xam_state()->profile_manager();
  if (!profiles->GetAccountCount()) {
    const bool created = profiles->CreateProfile("Player", true);
    std::printf("RUNNER PROFILE %s\n", created ? "created" : "could not be created");
  }
  // A game that starts another executable (a collection's menu) saves what to
  // run in launch_data.bin and asks for a restart: the console restarts the
  // title; here the process ends with status 42, and the next run (in the same
  // folder) carries on with the saved request.
  auto xam = emulator.kernel_state()->GetKernelModule<xe::kernel::xam::XamModule>("xam.xex");
  xe::kernel::xam::XamModule::restart_hook = [] {
    std::printf("RUNNER RESTART REQUESTED\n");
    xe::FlushLog();
    std::fflush(stdout);
    std::_Exit(42);
  };
  std::filesystem::path game = argv[1];
  xam->LoadLoaderData();
  if (!xam->loader_data().host_path.empty()) {
    game = xam->loader_data().host_path;
    std::printf("RUNNER RESUME %s in %s (%zu bytes of launch data)\n", xam->loader_data().launch_path.c_str(),
                game.string().c_str(), xam->loader_data().launch_data.size());
  }
  // Kept with a later request, so the restart finds the game again.
  xam->loader_data().host_path = xe::path_to_utf8(game);
  const auto launched = emulator.LaunchPath(game);
  if (!launched && std::getenv("XBOX360PS5_CHECK_MODULE_LOOKUP")) {
    auto* kernel = emulator.kernel_state();
    auto module = kernel->GetExecutableModule();
    if (!module || kernel->GetModule(module->name()).get() != module.get() ||
        kernel->GetModule(module->path()).get() != module.get() ||
        kernel->GetModule("PS5X360_nonexistent_module")) {
      std::fprintf(stderr, "FAIL: loaded module name/path or absent-module lookup\n");
      std::_Exit(10);
    }
    std::puts("PASS: loaded module name/path and absent-module lookup");
  }
  if (const char* expected = std::getenv("XBOX360PS5_EXPECT_PATCHES")) {
    if (xbox360ps5::LastAppliedPatches() != std::atoi(expected)) {
      std::fprintf(stderr, "PATCH CHECK FAIL: expected %s applied, got %d\n", expected, xbox360ps5::LastAppliedPatches());
      std::_Exit(9);
    }
    std::printf("PATCH CHECK PASS: hash %016llX, %d applied\n", (unsigned long long)xbox360ps5::LastModuleHash(), xbox360ps5::LastAppliedPatches());
  }
  std::printf("RUNNER LAUNCH %08x %s\n", unsigned(launched), game.string().c_str());
  std::fflush(stdout);
  const auto started = std::chrono::steady_clock::now();
  const auto end = started + std::chrono::seconds(std::atoi(argv[2]));
  auto next_shot = started + std::chrono::seconds(5);
  // XBOX360PS5_DUMP=addr,addr,...: the guest code at each address (hex), to
  // read what a hot function found by a console measurement does.
  if (const char* dump = std::getenv("XBOX360PS5_DUMP")) {
    for (const char* at = dump; *at;) {
      char* end = nullptr;
      const uint32_t address = uint32_t(std::strtoul(at, &end, 16));
      const uint8_t* bytes = emulator.memory()->TranslateVirtual<const uint8_t*>(address);
      std::printf("DUMP %08X ", address);
      for (int n = 0; n < 0x600; ++n) std::printf("%02x", bytes[n]);
      std::putchar('\n');
      at = *end == ',' ? end + 1 : end;
      if (end == at) break;
    }
    std::fflush(stdout);
  }
  const bool autopress = !std::getenv("XBOX360PS5_NO_AUTOPRESS");
  // XBOX360PS5_AUTOPRESS_AFTER=<seconds>: no presses before that time.
  const long press_after_ms = std::getenv("XBOX360PS5_AUTOPRESS_AFTER") ? std::atol(std::getenv("XBOX360PS5_AUTOPRESS_AFTER")) * 1000 : 0;
  const long start_until_ms = std::getenv("XBOX360PS5_START_UNTIL") ? std::atol(std::getenv("XBOX360PS5_START_UNTIL")) * 1000 : -1;
  const long press_until_ms = std::getenv("XBOX360PS5_AUTOPRESS_UNTIL") ? std::atol(std::getenv("XBOX360PS5_AUTOPRESS_UNTIL")) * 1000 : -1;
  const bool press_right = std::getenv("XBOX360PS5_AUTOPRESS_RIGHT") != nullptr;
  int shot = 0;
  while (std::chrono::steady_clock::now() < end) {
    context.Tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (pad) {
      const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count();
      const auto phase = elapsed_ms < press_after_ms ? 3999 : elapsed_ms % 4000;
      xbox360ps5::PadSample sample;
      sample.connected = true;
      const bool pressing = autopress && (press_until_ms < 0 || elapsed_ms < press_until_ms);
      if (pressing && phase < 100 && (start_until_ms < 0 || elapsed_ms < start_until_ms)) sample.buttons = 0x08; // START
      if (pressing && press_right && phase >= 1000 && phase < 1100) sample.buttons = 0x20;  // Right
      if (pressing && phase >= 2000 && phase < 2100) sample.buttons = 0x4000;               // A
      pad->Submit(sample);
    }
    if (argc == 4 && std::chrono::steady_clock::now() >= next_shot) {
      next_shot += std::chrono::seconds(5);
      xe::ui::RawImage image;
      if (emulator.graphics_system()->presenter()->CaptureGuestOutput(image) && image.width) {
        for (size_t at = 3; at < image.data.size(); at += 4) image.data[at] = 255;
        char name[512];
        std::snprintf(name, sizeof(name), "%s/shot-%03d.png", argv[3], shot++);
        stbi_write_png(name, int(image.width), int(image.height), 4, image.data.data(), int(image.stride));
      }
    }
  }
  std::fflush(stdout);
  if (!launched && emulator.graphics_system()->command_processor()) {
    auto* processor = emulator.graphics_system()->command_processor();
    std::printf("RUNNER OUTPUT: %llu refreshed, %llu submitted swaps\n",
                (unsigned long long)processor->refreshed_output_count(),
                (unsigned long long)processor->swap_count());
    std::fflush(stdout);
  }
  std::_Exit(0);
}
