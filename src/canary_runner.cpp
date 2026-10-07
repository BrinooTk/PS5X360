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
#include "xbox360ps5/motion_input.hpp"
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
#include "xenia/kernel/xmutant.h"
#include "xenia/kernel/xthread.h"
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
#include <fstream>
#include <thread>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "third_party/stb/stb_image_write.h"
namespace config {
void ReadGameConfig(const std::filesystem::path& file_path);  // config.cc, not in config.h.
}
namespace xe::kernel::xboxkrnl {
bool CheckVirtualCameraAbi(KernelState* kernel);
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
  if (std::getenv("XBOX360PS5_CHECK_NESTED_SUSPEND")) {
    std::puts("NESTED CHECK: creating suspended kernel thread"); std::fflush(stdout);
    std::atomic<bool> executed{false};
    auto thread = xe::kernel::object_ref<xe::kernel::XHostThread>(new xe::kernel::XHostThread(
        emulator.kernel_state(), 128 * 1024, xe::kernel::X_CREATE_SUSPENDED,
        [&] { executed.store(true); return 0; }, emulator.kernel_state()->GetSystemProcess()));
    uint32_t previous = 99;
    bool passed = thread->Create() == 0;
    std::puts("NESTED CHECK: created; applying second suspension"); std::fflush(stdout);
    passed = passed && thread->Suspend(&previous) == 0 && previous == 1;
    std::puts("NESTED CHECK: resuming first hold"); std::fflush(stdout);
    passed = passed && thread->Resume(&previous) == 0 && previous == 2;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    passed = passed && !executed.load();
    std::puts("NESTED CHECK: resuming final hold"); std::fflush(stdout);
    passed = passed && thread->Resume(&previous) == 0 && previous == 1;
    for (int n = 0; n < 100 && !executed.load(); ++n)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    passed = passed && executed.load();
    std::puts(passed ? "PASS: nested guest suspensions retain the first hold and execute after the final resume" : "FAIL: nested guest suspension");
    std::fflush(stdout);
    std::_Exit(passed ? 0 : 27);
  }
  if (!profiles->GetAccountCount()) {
    const bool created = profiles->CreateProfile("Player", true);
    std::printf("RUNNER PROFILE %s\n", created ? "created" : "could not be created");
  }
  // Host-only regression: removing an account must preserve its save bytes,
  // and rescanning must not bring the archived account back.
  if (std::getenv("XBOX360PS5_CHECK_PROFILE_ARCHIVE")) {
    bool passed = profiles->CreateProfile("Archive Test", false);
    uint64_t target = 0;
    for (const auto& [id, account] : *profiles->GetAccounts())
      if (account.GetGamertagString() == "Archive Test") target = id;
    const auto source = profiles->GetProfileContentPath(target);
    if (target) {
      profiles->Login(target, 1, false);
      { std::ofstream save(source / "regression-save.bin", std::ios::binary); save << "preserved-save"; passed = passed && bool(save); }
      passed = passed && profiles->ArchiveProfile(target);
      passed = passed && !profiles->GetAccounts()->count(target) && !profiles->GetProfile(uint8_t(1)) && !std::filesystem::exists(source);
      std::ifstream save(emulator.content_root() / "removed-profiles" / source.filename() / "regression-save.bin", std::ios::binary);
      std::string bytes; std::getline(save, bytes);
      passed = passed && bytes == "preserved-save";
      profiles->ReloadProfiles();
      passed = passed && !profiles->GetAccounts()->count(target);
      passed = passed && !profiles->ArchiveProfile(target);
    } else passed = false;
    std::puts(passed ? "PASS: profile archive retains saves, signs out, survives rescan and rejects repeat removal" : "FAIL: profile archive");
    std::fflush(stdout);
    std::_Exit(passed ? 0 : 26);
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
  // Explicit host-only virtual-camera fixture; never enabled on the console.
  std::jthread virtual_pose;
  if (std::getenv("XBOX360PS5_VIRTUAL_KINECT")) {
    xbox360ps5::motion::SetEnabled(true);
    virtual_pose = std::jthread([](std::stop_token stop) {
      using namespace xbox360ps5::motion;
      while (!stop.stop_requested()) {
        Frame f; f.tracked=true;f.received=std::chrono::steady_clock::now();
        const float xyz[20][3]={{0,0,0},{0,.2f,0},{0,.45f,0},{0,.65f,0},{-.2f,.4f,0},{-.35f,.2f,0},{-.4f,0,0},{-.4f,-.03f,0},{.2f,.4f,0},{.35f,.6f,0},{.4f,.85f,0},{.4f,.9f,0},{-.12f,0,0},{-.12f,-.4f,0},{-.12f,-.8f,0},{-.12f,-.85f,-.12f},{.12f,0,0},{.12f,-.4f,0},{.12f,-.8f,0},{.12f,-.85f,-.12f}};
        // The bridge's axes: +x is the right of the camera picture, which is the
        // left side of a body that faces the camera. The table has the right arm
        // at +x, so x is mirrored here. The right arm goes down and up again
        // every six seconds: a title may want to see the hand being raised.
        for (int i=0;i<20;++i)f.joints[i]={-xyz[i][0],xyz[i][1],xyz[i][2],1};
        const auto since=std::chrono::duration_cast<std::chrono::milliseconds>(f.received.time_since_epoch()).count();
        if (!std::getenv("XBOX360PS5_VIRTUAL_KINECT_STILL") && since%6000<2000) {
          f.joints[9]={-.3f,.2f,0,1};f.joints[10]={-.33f,0,0,1};f.joints[11]={-.33f,-.05f,0,1};
        }
        {std::lock_guard lock(mutex);f.sequence=latest.sequence+1;f.session=1;latest=f;}
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
      }
    });
  }
  if (std::getenv("XBOX360PS5_CHECK_CAMERA_ABI")) {
    xbox360ps5::motion::SetEnabled(true);
    const bool passed = xe::kernel::xboxkrnl::CheckVirtualCameraAbi(emulator.kernel_state());
    std::puts(passed ? "PASS: virtual camera state/version/table, invalid buffers, async depth and guest PPC callback, stop" : "FAIL: virtual camera ABI");
    std::fflush(stdout);
    std::_Exit(passed ? 0 : 25);
  }
  const auto launched = emulator.LaunchPath(game);
  if (!launched && std::getenv("XBOX360PS5_CHECK_GUEST_PACKAGE")) {
    using xe::X_RESULT;
    auto* manager = emulator.kernel_state()->content_manager();
    uint32_t license = 0;
    const auto opened = manager->OpenContentFromGuestFile("research-package-check", "game:\\Database.xmplr", license);
    if (opened || !emulator.file_system()->ResolvePath("research-package-check:")) std::_Exit(14);
    if (manager->OpenContentFromGuestFile("research-package-check", "game:\\Database.xmplr", license) != X_ERROR_ALREADY_EXISTS) std::_Exit(15);
    if (manager->CloseContent("research-package-check")) std::_Exit(16);
    if (emulator.file_system()->IsSymbolicLinkRegistered("research-package-check:")) std::_Exit(17);
    if (manager->OpenContentFromGuestFile("research-package-invalid", "game:\\default.xex", license) != X_ERROR_FILE_NOT_FOUND) std::_Exit(18);
    if (manager->OpenContentFromGuestFile("research-package-missing", "game:\\does-not-exist.xmplr", license) != X_ERROR_FILE_NOT_FOUND) std::_Exit(19);
    std::puts("PASS: nested guest package mounted, duplicate rejected, closed cleanly, invalid and missing packages rejected");
    std::fflush(stdout);
    std::_Exit(0);
  }
  if (!launched && std::getenv("XBOX360PS5_CHECK_GUEST_MUTANT")) {
    using xe::X_STATUS;
    auto* kernel = emulator.kernel_state();
    const uint32_t address = emulator.memory()->SystemHeapAlloc(sizeof(xe::kernel::X_KMUTANT));
    if (!address) std::_Exit(20);
    auto* native = emulator.memory()->TranslateVirtual<xe::kernel::X_KMUTANT*>(address);
    std::memset(native, 0, sizeof(*native));
    native->header.type = xe::kernel::MutantObject;
    native->header.signal_state = 1;
    auto mutant = xe::kernel::XObject::GetNativeObject<xe::kernel::XMutant>(kernel, native, xe::kernel::MutantObject);
    uint64_t immediate = 0;
    if (!mutant || mutant->Wait(0, 0, 0, &immediate) != X_STATUS_SUCCESS ||
        mutant->Wait(0, 0, 0, &immediate) != X_STATUS_SUCCESS) std::_Exit(21);
    auto foreign_wait = [&]() {
      uint32_t status = 0;
      std::thread other([&]() {
        uint64_t timeout = 0;
        status = mutant->Wait(0, 0, 0, &timeout);
        if (status == X_STATUS_SUCCESS) mutant->ReleaseMutant(0, false, false);
      });
      other.join();
      return status;
    };
    if (foreign_wait() != X_STATUS_TIMEOUT) std::_Exit(22);
    if (mutant->ReleaseMutant(0, false, false) != X_STATUS_SUCCESS ||
        foreign_wait() != X_STATUS_TIMEOUT) std::_Exit(23);
    if (mutant->ReleaseMutant(0, false, false) != X_STATUS_SUCCESS ||
        foreign_wait() != X_STATUS_SUCCESS) std::_Exit(24);
    std::puts("PASS: embedded guest mutant supports recursive ownership and excludes other threads until fully released");
    std::fflush(stdout);
    std::_Exit(0);
  }
  // Offline research: preserve the loaded guest image and resolved imports.
  // Opt-in host-only output, never part of console logging or AutoLog.
  if (!launched) {
    if (const char* output = std::getenv("XBOX360PS5_INSPECT_IMAGE")) {
      const auto module = emulator.kernel_state()->GetExecutableModule();
      const auto* xex = module ? module->xex_module() : nullptr;
      if (!xex) std::_Exit(11);
      FILE* file = std::fopen(output, "wb");
      if (!file) std::_Exit(12);
      const uint32_t size = xex->image_size();
      const auto* bytes = emulator.memory()->TranslateVirtual<const uint8_t*>(xex->base_address());
      const bool written = std::fwrite(bytes, 1, size, file) == size;
      std::fclose(file);
      std::printf("IMAGE %08X %08X %s\n", xex->base_address(), size, output);
      for (const auto& library : *xex->import_libraries()) {
        for (const auto& imported : library.imports) {
          std::printf("IMPORT %s %04X %08X %08X\n", library.name.c_str(),
                      imported.ordinal, imported.value_address, imported.thunk_address);
        }
      }
      std::fflush(stdout);
      std::_Exit(written ? 0 : 13);
    }
  }
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
    if (xbox360ps5::motion::enabled.load()) std::printf("RUNNER MOTION: %s\n", xbox360ps5::motion::Status().c_str());
    std::fflush(stdout);
  }
  std::_Exit(0);
}
