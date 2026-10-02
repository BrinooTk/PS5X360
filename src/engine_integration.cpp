// SPDX-License-Identifier: MIT
#include "xenia/emulator.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xbox360ps5/native_audio.hpp"
#include "xbox360ps5/dualsense_input.hpp"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/kernel/user_module.h"
#include <cstdio>
#include <filesystem>
#include <vector>
int main(int argc, char** argv) {
  if (argc != 1 && (argc != 3 || std::string_view(argv[1]) != "--load-xex")) return 3;
  const auto root = std::filesystem::temp_directory_path() / "xbox360ps5-engine-lifecycle";
  std::filesystem::create_directories(root);
  {
    xe::Emulator emulator("", root, root / "content", root / "cache");
    const auto status = emulator.Setup(nullptr, nullptr, true,
        [](xe::cpu::Processor* processor) -> std::unique_ptr<xe::apu::AudioSystem> {
          return std::make_unique<xbox360ps5::NativeAudioSystem>(processor, 0xff);
        },
        []() -> std::unique_ptr<xe::gpu::GraphicsSystem> {
          return std::make_unique<xe::gpu::vulkan::VulkanGraphicsSystem>();
        }, nullptr);
    std::printf("ACTUAL ENGINE SETUP %08x\n", unsigned(status));
    if (status != 0) return 1;
    if (!emulator.kernel_state() || !emulator.audio_system() ||
        !emulator.graphics_system() || !emulator.processor() || !emulator.file_system()) return 2;
    std::puts("KERNEL CPU VFS XMA VULKAN INITIALIZED (no game executed, no display/audio port claimed)");
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
