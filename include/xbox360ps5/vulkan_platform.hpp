// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <string>
#include <vulkan/vulkan.h>

namespace xbox360ps5 {
// A linked RADV ICD and a host Vulkan loader expose the same GIPA contract.
// The driver must outlive this object. No desktop window system is required.
class VulkanPlatform {
public:
  explicit VulkanPlatform(PFN_vkGetInstanceProcAddr driver) : gipa_(driver) {}
  ~VulkanPlatform() { Reset(); }
  VulkanPlatform(const VulkanPlatform &) = delete;
  VulkanPlatform &operator=(const VulkanPlatform &) = delete;
  bool Initialize(bool display, uint32_t width = 1920, uint32_t height = 1080,
                  uint32_t refresh_millihz = 60000);
  void Reset();
  const std::string &error() const { return error_; }
  VkInstance instance() const { return instance_; }
  VkPhysicalDevice physical_device() const { return physical_; }
  VkDevice device() const { return device_; }
  VkSurfaceKHR surface() const { return surface_; }
  VkQueue queue() const { return queue_; }
  uint32_t queue_family() const { return family_; }
  VkExtent2D extent() const { return extent_; }
  uint32_t refresh_millihz() const { return refresh_; }
  template <typename T> T InstanceFunction(const char *name) const {
    return gipa_ ? reinterpret_cast<T>(gipa_(instance_, name)) : nullptr;
  }
  template <typename T> T DeviceFunction(const char *name) const {
    return gdpa_ ? reinterpret_cast<T>(gdpa_(device_, name)) : nullptr;
  }

private:
  bool Fail(const std::string &message);
  bool CreateSurface(VkPhysicalDevice physical, uint32_t width, uint32_t height,
                     uint32_t refresh);
  PFN_vkGetInstanceProcAddr gipa_{};
  PFN_vkGetDeviceProcAddr gdpa_{};
  PFN_vkDestroyInstance destroy_instance_{};
  PFN_vkDestroySurfaceKHR destroy_surface_{};
  PFN_vkDestroyDevice destroy_device_{};
  PFN_vkDeviceWaitIdle wait_idle_{};
  VkInstance instance_{};
  VkPhysicalDevice physical_{};
  VkDevice device_{};
  VkSurfaceKHR surface_{};
  VkQueue queue_{};
  uint32_t family_{};
  VkExtent2D extent_{};
  uint32_t refresh_{};
  std::string error_;
};
} // namespace xbox360ps5
