// SPDX-License-Identifier: MIT
// Minimal synthetic Xenos shader -> real translator -> actual Vulkan module.
#include "xenia/gpu/spirv_shader.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/ui/vulkan/vulkan_instance.h"
#include <cstdio>
#include <cstring>
int main() {
  auto instance = xe::ui::vulkan::VulkanInstance::Create(false, false);
  if (!instance) return 1;
  std::vector<VkPhysicalDevice> physicals;
  instance->EnumeratePhysicalDevices(physicals);
  for (auto physical : physicals) {
    auto device = xe::ui::vulkan::VulkanDevice::CreateIfSupported(instance.get(), physical, true, false);
    if (!device) continue;
    xe::gpu::SpirvShaderTranslator::Features features(device.get());
    xe::gpu::SpirvShaderTranslator translator(features, false, false, false);
    // Two packed 48-bit CF instructions: EXEC_END with zero ALU count, NOP.
    const uint32_t ucode[] = {0, 0x2000, 0};
    for (auto type : {xe::gpu::xenos::ShaderType::kVertex, xe::gpu::xenos::ShaderType::kPixel}) {
      xe::gpu::SpirvShader shader(type, 0, ucode, 3, std::endian::native);
      xe::StringBuffer disassembly;
      shader.AnalyzeUcode(disassembly);
      const uint64_t modification = type == xe::gpu::xenos::ShaderType::kVertex
          ? translator.GetDefaultVertexShaderModification(1)
          : translator.GetDefaultPixelShaderModification(1);
      auto translation = shader.GetOrCreateTranslation(modification);
      if (!translation || !translator.TranslateAnalyzedShader(*translation)) {
        std::puts("FAIL actual Xenos translation"); return 1;
      }
      const auto& binary = translation->translated_binary();
      if (binary.size() < 20 || binary.size() % 4) return 1;
      std::vector<uint32_t> words(binary.size() / 4);
      std::memcpy(words.data(), binary.data(), binary.size());
      if (words[0] != 0x07230203) return 1;
      VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      info.codeSize = binary.size(); info.pCode = words.data();
      VkShaderModule module = VK_NULL_HANDLE;
      const auto result = device->functions().vkCreateShaderModule(device->device(), &info, nullptr, &module);
      if (result != VK_SUCCESS || !module) return 1;
      device->functions().vkDestroyShaderModule(device->device(), module, nullptr);
      std::printf("ACTUAL XENOS %s -> SPIR-V %zu bytes -> real Vulkan module (synthetic)\n",
                  type == xe::gpu::xenos::ShaderType::kVertex ? "VS" : "PS", binary.size());
    }
    return 0;
  }
  return 1;
}
