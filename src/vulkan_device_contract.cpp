// SPDX-License-Identifier: MIT
// Real Xenia instance and GPU-emulation feature negotiation, no mock driver.
#include "xenia/ui/vulkan/vulkan_instance.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include <cstdio>
int main() {
  auto instance = xe::ui::vulkan::VulkanInstance::Create(false, false);
  if (!instance) { std::puts("FAIL actual Xenia Vulkan instance"); return 1; }
  std::vector<VkPhysicalDevice> devices;
  instance->EnumeratePhysicalDevices(devices);
  for (auto physical : devices) {
    auto device = xe::ui::vulkan::VulkanDevice::CreateIfSupported(instance.get(), physical, true, false);
    if (!device) continue;
    auto wait = reinterpret_cast<PFN_vkDeviceWaitIdle>(
        instance->functions().vkGetDeviceProcAddr(device->device(), "vkDeviceWaitIdle"));
    if (!wait) { std::puts("FAIL real vkDeviceWaitIdle unavailable"); return 1; }
    auto result = wait(device->device());
    std::printf("ACTUAL XENIA VULKAN GPU DEVICE queue=%u wait=%d (no game rendered)\n",
                device->queue_family_graphics_compute(), int(result));
    return result == VK_SUCCESS ? 0 : 1;
  }
  std::puts("FAIL no device satisfies actual Xenia GPU requirements");
  return 1;
}
