// SPDX-License-Identifier: BSD-3-Clause
// VK_KHR_display is the PS5 RADV VideoOut interface. Written for this port;
// reference implementations and revisions are recorded in
// docs/PS5_REFERENCES.md.
#include "xbox360ps5/vulkan_platform.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace xbox360ps5 {
namespace {
// Enumeration may grow between calls. Bound both allocation and retries.
template <typename T, typename F>
bool Enumerate(std::vector<T> &values, F function) {
  for (unsigned attempt = 0; attempt < 8; ++attempt) {
    uint32_t count = 0;
    VkResult result = function(&count, nullptr);
    if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count > 16384)
      return false;
    values.resize(count);
    if (!count)
      return true;
    result = function(&count, values.data());
    if (result == VK_INCOMPLETE)
      continue;
    if (result != VK_SUCCESS || count > values.size())
      return false;
    values.resize(count);
    return true;
  }
  return false;
}
bool HasExtension(const std::vector<VkExtensionProperties> &extensions,
                  const char *name) {
  return std::any_of(extensions.begin(), extensions.end(),
                     [name](const auto &e) {
                       return std::strcmp(e.extensionName, name) == 0;
                     });
}
uint64_t Distance(uint32_t a, uint32_t b) {
  return a > b ? uint64_t(a) - b : uint64_t(b) - a;
}
bool Fits(uint32_t value, uint32_t minimum, uint32_t maximum) {
  return value >= minimum && value <= maximum;
}
} // namespace

void VulkanPlatform::Reset() {
  if (device_) {
    if (wait_idle_)
      wait_idle_(device_);
    if (destroy_device_)
      destroy_device_(device_, nullptr);
  }
  if (surface_ && destroy_surface_)
    destroy_surface_(instance_, surface_, nullptr);
  if (instance_ && destroy_instance_)
    destroy_instance_(instance_, nullptr);
  instance_ = VK_NULL_HANDLE;
  physical_ = VK_NULL_HANDLE;
  device_ = VK_NULL_HANDLE;
  surface_ = VK_NULL_HANDLE;
  queue_ = VK_NULL_HANDLE;
  gdpa_ = nullptr;
  destroy_instance_ = nullptr;
  destroy_surface_ = nullptr;
  destroy_device_ = nullptr;
  wait_idle_ = nullptr;
  family_ = 0;
  extent_ = {};
  refresh_ = 0;
}
bool VulkanPlatform::Fail(const std::string &message) {
  error_ = message;
  Reset();
  return false;
}

bool VulkanPlatform::CreateSurface(VkPhysicalDevice physical, uint32_t width,
                                   uint32_t height, uint32_t refresh) {
  return CreateDisplaySurface(gipa_, instance_, physical, width, height, refresh,
                              surface_, extent_, refresh_, error_);
}
bool CreateDisplaySurface(PFN_vkGetInstanceProcAddr driver, VkInstance instance_,
                          VkPhysicalDevice physical, uint32_t width, uint32_t height,
                          uint32_t refresh, VkSurfaceKHR& surface_, VkExtent2D& extent_,
                          uint32_t& refresh_, std::string& error_) {
  surface_ = VK_NULL_HANDLE; extent_ = {}; refresh_ = 0; error_.clear();
  if (!driver || !instance_ || !physical) { error_ = "invalid display surface owner"; return false; }
#define LOAD(name) auto name = reinterpret_cast<PFN_##name>(driver(instance_, #name))
  LOAD(vkGetPhysicalDeviceDisplayPropertiesKHR);
  LOAD(vkGetDisplayModePropertiesKHR);
  LOAD(vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
  LOAD(vkGetDisplayPlaneSupportedDisplaysKHR);
  LOAD(vkGetDisplayPlaneCapabilitiesKHR);
  LOAD(vkCreateDisplayPlaneSurfaceKHR);
#undef LOAD
  if (!vkGetPhysicalDeviceDisplayPropertiesKHR ||
      !vkGetDisplayModePropertiesKHR ||
      !vkGetPhysicalDeviceDisplayPlanePropertiesKHR ||
      !vkGetDisplayPlaneSupportedDisplaysKHR ||
      !vkGetDisplayPlaneCapabilitiesKHR || !vkCreateDisplayPlaneSurfaceKHR) {
    error_ = "VK_KHR_display dispatch incomplete";
    return false;
  }
  std::vector<VkDisplayPropertiesKHR> displays;
  std::vector<VkDisplayPlanePropertiesKHR> planes;
  if (!Enumerate(displays,
                 [&](auto n, auto p) {
                   return vkGetPhysicalDeviceDisplayPropertiesKHR(physical, n,
                                                                  p);
                 }) ||
      !Enumerate(planes, [&](auto n, auto p) {
        return vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical, n, p);
      })) {
    error_ = "display/plane enumeration failed";
    return false;
  }
  uint64_t best = std::numeric_limits<uint64_t>::max();
  VkDisplaySurfaceCreateInfoKHR chosen{};
  uint32_t chosen_refresh = 0;
  for (const auto &display : displays) {
    if (!(display.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
      continue;
    std::vector<VkDisplayModePropertiesKHR> modes;
    if (!Enumerate(modes, [&](auto n, auto p) {
          return vkGetDisplayModePropertiesKHR(physical, display.display, n, p);
        }))
      continue;
    for (uint32_t plane = 0; plane < planes.size(); ++plane) {
      // Never take a plane currently assigned to another display.
      if (planes[plane].currentDisplay &&
          planes[plane].currentDisplay != display.display)
        continue;
      std::vector<VkDisplayKHR> supported;
      if (!Enumerate(supported,
                     [&](auto n, auto p) {
                       return vkGetDisplayPlaneSupportedDisplaysKHR(
                           physical, plane, n, p);
                     }) ||
          std::find(supported.begin(), supported.end(), display.display) ==
              supported.end())
        continue;
      for (const auto &mode : modes) {
        VkDisplayPlaneCapabilitiesKHR caps{};
        if (vkGetDisplayPlaneCapabilitiesKHR(physical, mode.displayMode, plane,
                                             &caps) != VK_SUCCESS ||
            !(caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR))
          continue;
        const auto size = mode.parameters.visibleRegion;
        if (!size.width || !size.height || !mode.parameters.refreshRate ||
            !Fits(size.width, caps.minSrcExtent.width,
                  caps.maxSrcExtent.width) ||
            !Fits(size.height, caps.minSrcExtent.height,
                  caps.maxSrcExtent.height) ||
            !Fits(size.width, caps.minDstExtent.width,
                  caps.maxDstExtent.width) ||
            !Fits(size.height, caps.minDstExtent.height,
                  caps.maxDstExtent.height) ||
            caps.minSrcPosition.x > 0 || caps.maxSrcPosition.x < 0 ||
            caps.minSrcPosition.y > 0 || caps.maxSrcPosition.y < 0 ||
            caps.minDstPosition.x > 0 || caps.maxDstPosition.x < 0 ||
            caps.minDstPosition.y > 0 || caps.maxDstPosition.y < 0)
          continue;
        const uint64_t score =
            (Distance(size.width, width) + Distance(size.height, height)) *
                1000000000ull +
            Distance(mode.parameters.refreshRate, refresh);
        if (score >= best)
          continue;
        best = score;
        chosen = {VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
        chosen.displayMode = mode.displayMode;
        chosen.planeIndex = plane;
        chosen.planeStackIndex = planes[plane].currentStackIndex;
        chosen.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        chosen.globalAlpha = 1.0f;
        chosen.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
        chosen.imageExtent = size;
        chosen_refresh = mode.parameters.refreshRate;
      }
    }
  }
  if (best == std::numeric_limits<uint64_t>::max()) {
    error_ = "no compatible display mode and plane";
    return false;
  }
  VkSurfaceKHR created_surface = VK_NULL_HANDLE;
  const VkResult result = vkCreateDisplayPlaneSurfaceKHR(
      instance_, &chosen, nullptr, &created_surface);
  if (result != VK_SUCCESS) {
    error_ = "display surface creation failed: " + std::to_string(result);
    return false;
  }
  surface_ = created_surface;
  extent_ = chosen.imageExtent;
  refresh_ = chosen_refresh;
  return true;
}

bool VulkanPlatform::Initialize(bool display, uint32_t width, uint32_t height,
                                uint32_t refresh) {
  Reset();
  error_.clear();
  if (!gipa_)
    return Fail("no Vulkan driver entry point");
  if (display && (!width || !height || !refresh))
    return Fail("invalid display request");
  const auto create_instance =
      InstanceFunction<PFN_vkCreateInstance>("vkCreateInstance");
  const auto enumerate_extensions =
      InstanceFunction<PFN_vkEnumerateInstanceExtensionProperties>(
          "vkEnumerateInstanceExtensionProperties");
  if (!create_instance || !enumerate_extensions)
    return Fail("global Vulkan dispatch incomplete");
  std::vector<VkExtensionProperties> extensions;
  if (!Enumerate(extensions, [&](auto n, auto p) {
        return enumerate_extensions(nullptr, n, p);
      }))
    return Fail("instance extension enumeration failed");
  const char *requested[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                             VK_KHR_DISPLAY_EXTENSION_NAME};
  if (display && (!HasExtension(extensions, requested[0]) ||
                  !HasExtension(extensions, requested[1])))
    return Fail("driver lacks VK_KHR_surface or VK_KHR_display");
  const VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              nullptr,
                              "Xbox360PS5 platform probe",
                              1,
                              "Xenia PS5 platform",
                              1,
                              VK_API_VERSION_1_1};
  VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  info.pApplicationInfo = &app;
  info.enabledExtensionCount = display ? 2 : 0;
  info.ppEnabledExtensionNames = display ? requested : nullptr;
  VkInstance created_instance = VK_NULL_HANDLE;
  VkResult result = create_instance(&info, nullptr, &created_instance);
  if (result != VK_SUCCESS)
    return Fail("instance creation failed: " + std::to_string(result));
  instance_ = created_instance;
  // Vulkan only guarantees instance-level entry points with a valid instance.
  destroy_instance_ =
      InstanceFunction<PFN_vkDestroyInstance>("vkDestroyInstance");
  if (!destroy_instance_)
    return Fail("instance destructor unavailable (invalid driver)");
  destroy_surface_ =
      InstanceFunction<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR");
  if (display && !destroy_surface_)
    return Fail("surface destructor unavailable");
  const auto enumerate_devices =
      InstanceFunction<PFN_vkEnumeratePhysicalDevices>(
          "vkEnumeratePhysicalDevices");
  const auto enumerate_device_extensions =
      InstanceFunction<PFN_vkEnumerateDeviceExtensionProperties>(
          "vkEnumerateDeviceExtensionProperties");
  const auto queue_properties =
      InstanceFunction<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
          "vkGetPhysicalDeviceQueueFamilyProperties");
  const auto surface_support =
      InstanceFunction<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
          "vkGetPhysicalDeviceSurfaceSupportKHR");
  const auto create_device =
      InstanceFunction<PFN_vkCreateDevice>("vkCreateDevice");
  gdpa_ = InstanceFunction<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
  // Destructors are instance commands too; avoid leaking if GDPA is incomplete.
  destroy_device_ = InstanceFunction<PFN_vkDestroyDevice>("vkDestroyDevice");
  if (!enumerate_devices || !enumerate_device_extensions || !queue_properties ||
      !create_device || !gdpa_ || !destroy_device_ ||
      (display && !surface_support))
    return Fail("device dispatch incomplete");
  std::vector<VkPhysicalDevice> devices;
  if (!Enumerate(devices, [&](auto n, auto p) {
        return enumerate_devices(instance_, n, p);
      }))
    return Fail("GPU enumeration failed");
  for (auto physical : devices) {
    extensions.clear();
    if (!Enumerate(extensions, [&](auto n, auto p) {
          return enumerate_device_extensions(physical, nullptr, n, p);
        }))
      continue;
    if (display && !HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
      continue;
    uint32_t count = 0;
    queue_properties(physical, &count, nullptr);
    if (!count || count > 16384)
      continue;
    std::vector<VkQueueFamilyProperties> families(count);
    queue_properties(physical, &count, families.data());
    families.resize(std::min<size_t>(count, families.size()));
    if (display && !CreateSurface(physical, width, height, refresh))
      continue;
    for (uint32_t family = 0; family < families.size(); ++family) {
      constexpr auto flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
      if (!families[family].queueCount ||
          (families[family].queueFlags & flags) != flags)
        continue;
      VkBool32 supported = VK_FALSE;
      if (display && (surface_support(physical, family, surface_, &supported) !=
                          VK_SUCCESS ||
                      !supported))
        continue;
      const float priority = 1.0f;
      VkDeviceQueueCreateInfo queue_info{
          VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
      queue_info.queueFamilyIndex = family;
      queue_info.queueCount = 1;
      queue_info.pQueuePriorities = &priority;
      const char *swapchain = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
      VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
      device_info.queueCreateInfoCount = 1;
      device_info.pQueueCreateInfos = &queue_info;
      device_info.enabledExtensionCount = display ? 1 : 0;
      device_info.ppEnabledExtensionNames = display ? &swapchain : nullptr;
      VkDevice created_device = VK_NULL_HANDLE;
      result = create_device(physical, &device_info, nullptr, &created_device);
      if (result != VK_SUCCESS) {
        error_ = "device creation failed: " + std::to_string(result);
        continue;
      }
      device_ = created_device;
      wait_idle_ = DeviceFunction<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle");
      const auto get_queue =
          DeviceFunction<PFN_vkGetDeviceQueue>("vkGetDeviceQueue");
      if (!wait_idle_ || !get_queue)
        return Fail("logical device dispatch incomplete");
      get_queue(device_, family, 0, &queue_);
      if (!queue_)
        return Fail("driver returned an empty queue");
      family_ = family;
      physical_ = physical;
      error_.clear();
      return true;
    }
    if (surface_) {
      destroy_surface_(instance_, surface_, nullptr);
      surface_ = VK_NULL_HANDLE;
      extent_ = {};
      refresh_ = 0;
    }
  }
  return Fail(error_.empty() ? "no suitable graphics/compute/presentation queue"
                             : error_);
}
} // namespace xbox360ps5
