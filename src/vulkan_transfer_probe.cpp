// SPDX-License-Identifier: BSD-3-Clause
// Real driver execution/readback on host; not a PS5 hardware validation.
#include "xbox360ps5/vulkan_platform.hpp"
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <stdexcept>
using xbox360ps5::VulkanPlatform;
namespace {
void Require(VkResult result, const char *operation) {
  if (result != VK_SUCCESS)
    throw std::runtime_error(std::string(operation) + ": " +
                             std::to_string(result));
}
struct Transfer {
  VulkanPlatform &platform;
  VkBuffer source{}, destination{};
  VkDeviceMemory source_memory{}, destination_memory{};
  VkCommandPool pool{};
  VkFence fence{};
  void *mapped{};
  PFN_vkDestroyBuffer destroy_buffer{};
  PFN_vkFreeMemory free_memory{};
  PFN_vkDestroyCommandPool destroy_pool{};
  PFN_vkDestroyFence destroy_fence{};
  PFN_vkUnmapMemory unmap{};
  PFN_vkDeviceWaitIdle idle{};
  bool submitted = false;
  ~Transfer() {
    const auto device = platform.device();
    if (submitted)
      idle(device);
    if (mapped)
      unmap(device, destination_memory);
    if (fence)
      destroy_fence(device, fence, nullptr);
    if (pool)
      destroy_pool(device, pool, nullptr);
    if (destination)
      destroy_buffer(device, destination, nullptr);
    if (source)
      destroy_buffer(device, source, nullptr);
    if (destination_memory)
      free_memory(device, destination_memory, nullptr);
    if (source_memory)
      free_memory(device, source_memory, nullptr);
  }
  void Run() {
#define LOAD(name)                                                             \
  auto name = platform.DeviceFunction<PFN_##name>(#name);                      \
  if (!name)                                                                   \
  throw std::runtime_error("missing " #name)
    LOAD(vkCreateBuffer);
    LOAD(vkGetBufferMemoryRequirements);
    LOAD(vkAllocateMemory);
    LOAD(vkBindBufferMemory);
    LOAD(vkCreateCommandPool);
    LOAD(vkAllocateCommandBuffers);
    LOAD(vkBeginCommandBuffer);
    LOAD(vkCmdFillBuffer);
    LOAD(vkCmdPipelineBarrier);
    LOAD(vkCmdCopyBuffer);
    LOAD(vkEndCommandBuffer);
    LOAD(vkCreateFence);
    LOAD(vkQueueSubmit);
    LOAD(vkWaitForFences);
    LOAD(vkMapMemory);
    LOAD(vkInvalidateMappedMemoryRanges);
    LOAD(vkDestroyBuffer);
    LOAD(vkFreeMemory);
    LOAD(vkDestroyCommandPool);
    LOAD(vkDestroyFence);
    LOAD(vkUnmapMemory);
    LOAD(vkDeviceWaitIdle);
#undef LOAD
    destroy_buffer = vkDestroyBuffer;
    free_memory = vkFreeMemory;
    destroy_pool = vkDestroyCommandPool;
    destroy_fence = vkDestroyFence;
    unmap = vkUnmapMemory;
    idle = vkDeviceWaitIdle;
    const auto properties =
        platform.InstanceFunction<PFN_vkGetPhysicalDeviceMemoryProperties>(
            "vkGetPhysicalDeviceMemoryProperties");
    if (!properties)
      throw std::runtime_error("missing memory properties");
    VkPhysicalDeviceMemoryProperties memory{};
    properties(platform.physical_device(), &memory);
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = 4096;
    buffer_info.usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const auto device = platform.device();
    auto allocate = [&](VkBuffer &buffer, VkDeviceMemory &allocation,
                        bool host_visible) {
      Require(vkCreateBuffer(device, &buffer_info, nullptr, &buffer),
              "create buffer");
      VkMemoryRequirements requirements{};
      vkGetBufferMemoryRequirements(device, buffer, &requirements);
      uint32_t index = memory.memoryTypeCount;
      for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (!host_visible || (memory.memoryTypes[i].propertyFlags &
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))) {
          index = i;
          break;
        }
      }
      if (index == memory.memoryTypeCount)
        throw std::runtime_error("no compatible memory type");
      VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      info.allocationSize = requirements.size;
      info.memoryTypeIndex = index;
      Require(vkAllocateMemory(device, &info, nullptr, &allocation),
              "allocate buffer memory");
      Require(vkBindBufferMemory(device, buffer, allocation, 0),
              "bind buffer memory");
    };
    allocate(source, source_memory, false);
    allocate(destination, destination_memory, true);
    VkCommandPoolCreateInfo pool_info{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = platform.queue_family();
    Require(vkCreateCommandPool(device, &pool_info, nullptr, &pool),
            "create command pool");
    VkCommandBufferAllocateInfo commands{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commands.commandPool = pool;
    commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commands.commandBufferCount = 1;
    VkCommandBuffer command{};
    Require(vkAllocateCommandBuffers(device, &commands, &command),
            "allocate command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Require(vkBeginCommandBuffer(command, &begin), "begin command buffer");
    constexpr uint32_t pattern = 0x3605cafe;
    vkCmdFillBuffer(command, source, 0, 4096, pattern);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = source;
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                         &barrier, 0, nullptr);
    VkBufferCopy copy{0, 0, 4096};
    vkCmdCopyBuffer(command, source, destination, 1, &copy);
    barrier.buffer = destination;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier,
                         0, nullptr);
    Require(vkEndCommandBuffer(command), "end command buffer");
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    Require(vkCreateFence(device, &fence_info, nullptr, &fence),
            "create fence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    Require(vkQueueSubmit(platform.queue(), 1, &submit, fence),
            "submit transfer");
    submitted = true;
    const auto result =
        vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ull);
    // Do not free in-flight resources on timeout. The development process
    // exits.
    if (result == VK_TIMEOUT) {
      std::fprintf(stderr, "transfer timeout\n");
      std::_Exit(2);
    }
    Require(result, "wait for transfer");
    Require(
        vkMapMemory(device, destination_memory, 0, VK_WHOLE_SIZE, 0, &mapped),
        "map readback");
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = destination_memory;
    range.size = VK_WHOLE_SIZE;
    Require(vkInvalidateMappedMemoryRanges(device, 1, &range),
            "invalidate readback");
    const auto *words = static_cast<const uint32_t *>(mapped);
    for (unsigned i = 0; i < 1024; ++i)
      if (words[i] != pattern)
        throw std::runtime_error("readback mismatch at " + std::to_string(i));
    std::puts("VULKAN TRANSFER WORDS 1024 FAILURES 0 (host driver, not PS5)");
  }
};
} // namespace
int main() {
  void *loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!loader) {
    std::fprintf(stderr, "no host Vulkan loader\n");
    return 2;
  }
  int status = 0;
  {
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(loader, "vkGetInstanceProcAddr"));
    VulkanPlatform platform(gipa);
    if (!platform.Initialize(false)) {
      std::fprintf(stderr, "%s\n", platform.error().c_str());
      status = 1;
    } else {
      const auto properties =
          platform.InstanceFunction<PFN_vkGetPhysicalDeviceProperties>(
              "vkGetPhysicalDeviceProperties");
      if (!properties) {
        std::fprintf(stderr, "missing physical device properties\n");
        return 1;
      }
      VkPhysicalDeviceProperties info{};
      properties(platform.physical_device(), &info);
      std::printf(
          "HOST DEVICE %s TYPE %u API %u.%u (hardware validation: false)\n",
          info.deviceName, info.deviceType, VK_VERSION_MAJOR(info.apiVersion),
          VK_VERSION_MINOR(info.apiVersion));
      try {
        Transfer transfer{platform};
        transfer.Run();
      } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        status = 1;
      }
    }
  }
  dlclose(loader);
  return status;
}
