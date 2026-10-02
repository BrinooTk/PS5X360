// SPDX-License-Identifier: MIT
// Static RADV loading only. No device properties or feature results fabricated.
#include "xbox360ps5/radv_dispatch.hpp"
#include <cstring>
extern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance, const char*);
namespace xbox360ps5 {
namespace {
VkResult VKAPI_CALL StaticLayers(uint32_t* count, VkLayerProperties*) {
  if (!count) return VK_ERROR_INITIALIZATION_FAILED;
  *count = 0;  // No loader layers are linked into this static driver.
  return VK_SUCCESS;
}
PFN_vkVoidFunction VKAPI_CALL InstanceProc(VkInstance instance, const char* name) {
  if (!name) return nullptr;
  if (!std::strcmp(name, "vkGetInstanceProcAddr"))
    return reinterpret_cast<PFN_vkVoidFunction>(InstanceProc);
  if (!std::strcmp(name, "vkEnumerateInstanceLayerProperties"))
    return reinterpret_cast<PFN_vkVoidFunction>(StaticLayers);
  return vk_icdGetInstanceProcAddr(instance, name);
}
void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
  auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(
      vk_icdGetInstanceProcAddr(instance, "vkDestroyInstance"));
  if (destroy) destroy(instance, allocator);
}
}
PFN_vkVoidFunction RadvLoaderProc(const char* name) {
  if (!name) return nullptr;
  if (!std::strcmp(name, "vkDestroyInstance"))
    return reinterpret_cast<PFN_vkVoidFunction>(DestroyInstance);
  return InstanceProc(VK_NULL_HANDLE, name);
}
}
