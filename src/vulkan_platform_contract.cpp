// SPDX-License-Identifier: BSD-3-Clause
// Fault-injected API contract tests. These do NOT prove GPU execution.
#include "xbox360ps5/vulkan_platform.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using xbox360ps5::VulkanPlatform;
namespace {
template <typename T> T Handle(uintptr_t value) {
  return reinterpret_cast<T>(value);
}
struct State {
  bool extensions = true, overflow = false, unstable = false,
       retry_once = false;
  bool empty = false, surface_fail = false, device_fail = false;
  bool compute = true, present = true, alpha = true, busy_plane = false;
  bool missing_device_queue = false;
  unsigned destroyed_instances = 0, destroyed_surfaces = 0,
           destroyed_devices = 0;
  unsigned waited = 0, enumeration_retries = 0;
  uint32_t selected_plane = 0, selected_stack = 0, selected_family = 0;
  VkDisplayModeKHR selected_mode{};
} state;
template <typename T>
VkResult Output(uint32_t *count, T *output, const std::vector<T> &values) {
  if (!output) {
    *count = static_cast<uint32_t>(values.size());
    return VK_SUCCESS;
  }
  uint32_t n = std::min(*count, static_cast<uint32_t>(values.size()));
  for (uint32_t i = 0; i < n; ++i)
    output[i] = values[i];
  *count = n;
  return n == values.size() ? VK_SUCCESS : VK_INCOMPLETE;
}
VkExtensionProperties Extension(const char *name) {
  VkExtensionProperties property{};
  std::strncpy(property.extensionName, name,
               sizeof(property.extensionName) - 1);
  return property;
}
VkResult VKAPI_CALL Extensions(const char *, uint32_t *n,
                               VkExtensionProperties *p) {
  if (state.overflow) {
    *n = 20000;
    return VK_SUCCESS;
  }
  if (p &&
      (state.unstable || (state.retry_once && !state.enumeration_retries))) {
    ++state.enumeration_retries;
    return VK_INCOMPLETE;
  }
  return Output(n, p,
                state.extensions
                    ? std::vector{Extension(VK_KHR_SURFACE_EXTENSION_NAME),
                                  Extension(VK_KHR_DISPLAY_EXTENSION_NAME)}
                    : std::vector<VkExtensionProperties>{});
}
VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo *,
                                   const VkAllocationCallbacks *,
                                   VkInstance *instance) {
  *instance = Handle<VkInstance>(1);
  return VK_SUCCESS;
}
void VKAPI_CALL DestroyInstance(VkInstance, const VkAllocationCallbacks *) {
  ++state.destroyed_instances;
}
VkResult VKAPI_CALL Devices(VkInstance, uint32_t *n, VkPhysicalDevice *p) {
  return Output(n, p,
                state.empty ? std::vector<VkPhysicalDevice>{}
                            : std::vector{Handle<VkPhysicalDevice>(2),
                                          Handle<VkPhysicalDevice>(3)});
}
VkResult VKAPI_CALL DeviceExtensions(VkPhysicalDevice, const char *,
                                     uint32_t *n, VkExtensionProperties *p) {
  return Output(n, p, std::vector{Extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)});
}
void VKAPI_CALL Families(VkPhysicalDevice, uint32_t *n,
                         VkQueueFamilyProperties *p) {
  if (!p) {
    *n = 2;
    return;
  }
  p[0] = {};
  p[0].queueFlags = VK_QUEUE_TRANSFER_BIT;
  p[0].queueCount = 1;
  p[1] = {};
  p[1].queueFlags =
      VK_QUEUE_GRAPHICS_BIT | (state.compute ? VK_QUEUE_COMPUTE_BIT : 0);
  p[1].queueCount = 1;
  *n = 2;
}
VkResult VKAPI_CALL Displays(VkPhysicalDevice physical, uint32_t *n,
                             VkDisplayPropertiesKHR *p) {
  if (physical == Handle<VkPhysicalDevice>(2))
    return Output(n, p, std::vector<VkDisplayPropertiesKHR>{});
  VkDisplayPropertiesKHR property{};
  property.display = Handle<VkDisplayKHR>(4);
  property.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  return Output(n, p, std::vector{property});
}
VkResult VKAPI_CALL Planes(VkPhysicalDevice, uint32_t *n,
                           VkDisplayPlanePropertiesKHR *p) {
  VkDisplayPlanePropertiesKHR first{}, second{};
  second.currentStackIndex = 7;
  if (state.busy_plane)
    second.currentDisplay = Handle<VkDisplayKHR>(99);
  return Output(n, p, std::vector{first, second});
}
VkResult VKAPI_CALL SupportedDisplays(VkPhysicalDevice, uint32_t plane,
                                      uint32_t *n, VkDisplayKHR *p) {
  return Output(n, p,
                plane ? std::vector{Handle<VkDisplayKHR>(4)}
                      : std::vector<VkDisplayKHR>{});
}
VkResult VKAPI_CALL Modes(VkPhysicalDevice, VkDisplayKHR, uint32_t *n,
                          VkDisplayModePropertiesKHR *p) {
  return Output(n, p,
                std::vector<VkDisplayModePropertiesKHR>{
                    {Handle<VkDisplayModeKHR>(5), {{1920, 1080}, 120000}},
                    {Handle<VkDisplayModeKHR>(6), {{1920, 1080}, 59940}},
                    {Handle<VkDisplayModeKHR>(7), {{1280, 720}, 60000}}});
}
VkResult VKAPI_CALL Capabilities(VkPhysicalDevice, VkDisplayModeKHR, uint32_t,
                                 VkDisplayPlaneCapabilitiesKHR *p) {
  *p = {};
  p->supportedAlpha = state.alpha ? VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR
                                  : VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR;
  p->maxSrcExtent = p->maxDstExtent = {4096, 4096};
  return VK_SUCCESS;
}
VkResult VKAPI_CALL Surface(VkInstance,
                            const VkDisplaySurfaceCreateInfoKHR *info,
                            const VkAllocationCallbacks *,
                            VkSurfaceKHR *surface) {
  if (state.surface_fail)
    return VK_ERROR_INITIALIZATION_FAILED;
  state.selected_plane = info->planeIndex;
  state.selected_stack = info->planeStackIndex;
  state.selected_mode = info->displayMode;
  *surface = Handle<VkSurfaceKHR>(8);
  return VK_SUCCESS;
}
void VKAPI_CALL DestroySurface(VkInstance, VkSurfaceKHR,
                               const VkAllocationCallbacks *) {
  ++state.destroyed_surfaces;
}
VkResult VKAPI_CALL Support(VkPhysicalDevice, uint32_t, VkSurfaceKHR,
                            VkBool32 *p) {
  *p = state.present;
  return VK_SUCCESS;
}
VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice,
                                 const VkDeviceCreateInfo *info,
                                 const VkAllocationCallbacks *,
                                 VkDevice *device) {
  if (state.device_fail)
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
  state.selected_family = info->pQueueCreateInfos->queueFamilyIndex;
  *device = Handle<VkDevice>(9);
  return VK_SUCCESS;
}
void VKAPI_CALL DestroyDevice(VkDevice, const VkAllocationCallbacks *) {
  ++state.destroyed_devices;
}
VkResult VKAPI_CALL Wait(VkDevice) {
  ++state.waited;
  return VK_SUCCESS;
}
void VKAPI_CALL Queue(VkDevice, uint32_t, uint32_t, VkQueue *p) {
  *p = Handle<VkQueue>(10);
}
PFN_vkVoidFunction VKAPI_CALL GIPA(VkInstance instance, const char *name);
PFN_vkVoidFunction VKAPI_CALL GDPA(VkDevice, const char *name) {
  if (state.missing_device_queue && !std::strcmp(name, "vkGetDeviceQueue"))
    return nullptr;
  return GIPA(Handle<VkInstance>(1), name);
}
PFN_vkVoidFunction VKAPI_CALL GIPA(VkInstance instance, const char *name) {
  // Match the loader: instance-level commands are unavailable globally.
  if (!instance && std::strcmp(name, "vkCreateInstance") &&
      std::strcmp(name, "vkEnumerateInstanceExtensionProperties"))
    return nullptr;
#define FN(vkname, implementation)                                             \
  if (!std::strcmp(name, #vkname))                                             \
  return reinterpret_cast<PFN_vkVoidFunction>(implementation)
  FN(vkCreateInstance, CreateInstance);
  FN(vkEnumerateInstanceExtensionProperties, Extensions);
  FN(vkDestroyInstance, DestroyInstance);
  FN(vkEnumeratePhysicalDevices, Devices);
  FN(vkEnumerateDeviceExtensionProperties, DeviceExtensions);
  FN(vkGetPhysicalDeviceQueueFamilyProperties, Families);
  FN(vkGetPhysicalDeviceDisplayPropertiesKHR, Displays);
  FN(vkGetDisplayModePropertiesKHR, Modes);
  FN(vkGetPhysicalDeviceDisplayPlanePropertiesKHR, Planes);
  FN(vkGetDisplayPlaneSupportedDisplaysKHR, SupportedDisplays);
  FN(vkGetDisplayPlaneCapabilitiesKHR, Capabilities);
  FN(vkCreateDisplayPlaneSurfaceKHR, Surface);
  FN(vkDestroySurfaceKHR, DestroySurface);
  FN(vkGetPhysicalDeviceSurfaceSupportKHR, Support);
  FN(vkCreateDevice, CreateDevice);
  FN(vkGetDeviceProcAddr, GDPA);
  FN(vkDestroyDevice, DestroyDevice);
  FN(vkDeviceWaitIdle, Wait);
  FN(vkGetDeviceQueue, Queue);
#undef FN
  return nullptr;
}
unsigned cases = 0, failures = 0;
void Check(bool result, const char *description) {
  ++cases;
  if (!result)
    ++failures;
  std::printf("%s %s\n", result ? "PASS" : "FAIL", description);
}
} // namespace
int main() {
  VulkanPlatform absent(nullptr);
  Check(!absent.Initialize(false), "reject missing driver");
  VulkanPlatform platform(GIPA);
  state = {};
  Check(!platform.Initialize(true, 0, 1080), "reject invalid extent");
  state = {};
  state.extensions = false;
  Check(!platform.Initialize(true) && !state.destroyed_instances,
        "reject missing WSI before allocating instance");
  state = {};
  state.overflow = true;
  Check(!platform.Initialize(false), "bound driver enumeration allocation");
  state = {};
  state.unstable = true;
  Check(!platform.Initialize(false) && state.enumeration_retries == 8,
        "bound VK_INCOMPLETE retries");
  state = {};
  state.retry_once = true;
  Check(platform.Initialize(true), "recover from VK_INCOMPLETE");
  Check(platform.physical_device() == Handle<VkPhysicalDevice>(3),
        "select second GPU with display");
  Check(state.selected_plane == 1 && state.selected_stack == 7,
        "respect compatible plane and current stack");
  Check(state.selected_mode == Handle<VkDisplayModeKHR>(6) &&
            platform.refresh_millihz() == 59940,
        "choose 59.94Hz rather than 120Hz");
  Check(platform.extent().width == 1920 && platform.extent().height == 1080,
        "retain actual selected extent");
  Check(platform.queue_family() == 1 && platform.queue(),
        "select graphics compute and presentation queue");
  platform.Reset();
  Check(state.destroyed_devices == 1 && state.destroyed_surfaces == 1 &&
            state.destroyed_instances == 1 && state.waited == 1,
        "release device surface and instance once");
  platform.Reset();
  Check(state.destroyed_instances == 1, "reset is idempotent");
  state = {};
  Check(platform.Initialize(true, 1280, 720) && platform.extent().width == 1280,
        "select requested 720p mode");
  platform.Reset();
  state = {};
  state.empty = true;
  Check(!platform.Initialize(false) && state.destroyed_instances == 1,
        "empty GPU list releases instance");
  state = {};
  state.alpha = false;
  Check(!platform.Initialize(true) && !state.destroyed_surfaces,
        "reject unsupported opaque alpha");
  state = {};
  state.busy_plane = true;
  Check(!platform.Initialize(true), "do not steal another display plane");
  state = {};
  state.surface_fail = true;
  Check(!platform.Initialize(true) && state.destroyed_instances == 1,
        "surface failure releases instance");
  state = {};
  state.present = false;
  Check(!platform.Initialize(true) && state.destroyed_surfaces == 1,
        "reject queue without presentation and release surface");
  state = {};
  state.compute = false;
  Check(!platform.Initialize(true) && state.destroyed_surfaces == 1,
        "reject graphics-only queue");
  state = {};
  state.device_fail = true;
  Check(!platform.Initialize(true) && state.destroyed_surfaces == 1 &&
            state.destroyed_devices == 0,
        "device allocation failure releases surface");
  state = {};
  state.missing_device_queue = true;
  Check(!platform.Initialize(true) && state.destroyed_devices == 1 &&
            state.destroyed_surfaces == 1,
        "partial device dispatch cleanup");
  state = {};
  Check(platform.Initialize(false) && !platform.surface(),
        "offscreen initialization requires no WSI");
  Check(platform.Initialize(false) && state.destroyed_instances == 1 &&
            state.destroyed_devices == 1,
        "reinitialization releases old session");
  platform.Reset();
  std::printf(
      "VULKAN PLATFORM CASES %u FAILURES %u (mock dispatch, no hardware)\n",
      cases, failures);
  return failures ? 1 : 0;
}
