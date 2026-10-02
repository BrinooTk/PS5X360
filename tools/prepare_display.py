"""Connect real Xenia presentation to KHR_display on its existing instance."""
def generate_display(source, root):
    def put(relative, data, header=False):
        target = root / ("build/generated" if header else "build/generated-sources") / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_text() != data:
            target.write_text(data)
    def once(data, old, new):
        if data.count(old) != 1:
            raise RuntimeError(f"Display overlay anchor changed: {old[:80]}")
        return data.replace(old, new)
    data = (source / "src/xenia/ui/surface.h").read_text()
    data = once(data, "    kTypeIndex_Win32Hwnd,", "    kTypeIndex_Win32Hwnd,\n    kTypeIndex_KhrDisplay,")
    data = once(data, "    kTypeFlag_Win32Hwnd = TypeFlags(1) << kTypeIndex_Win32Hwnd,", "    kTypeFlag_Win32Hwnd = TypeFlags(1) << kTypeIndex_Win32Hwnd,\n    kTypeFlag_KhrDisplay = TypeFlags(1) << kTypeIndex_KhrDisplay,")
    put("xenia/ui/surface.h", data, True)
    data = (source / "src/xenia/ui/vulkan/vulkan_instance.h").read_text()
    data = once(data, "    bool ext_KHR_surface = false;  // #1", "    bool ext_KHR_surface = false;  // #1\n    bool ext_KHR_display = false;  // PS5 VideoOut, #3")
    put("xenia/ui/vulkan/vulkan_instance.h", data, True)
    data = (root / "build/generated-sources/vulkan_instance.cc").read_text()
    marker = "  std::vector<const char*> enabled_extensions;"
    data = once(data, marker, '''#if XE_PLATFORM_PS5
  if (with_surface) requested_extensions.emplace("VK_KHR_display", &vulkan_instance->extensions_.ext_KHR_display);
#endif
''' + marker)
    put("vulkan_instance.cc", data)
    data = (source / "src/xenia/ui/vulkan/vulkan_presenter.cc").read_text()
    marker = '#include "xenia/ui/vulkan/vulkan_util.h"'
    data = once(data, marker, marker + '''
#if XE_PLATFORM_PS5
#include "xbox360ps5/display_surface.hpp"
#include "xbox360ps5/vulkan_platform.hpp"
#endif''')
    marker = "  Surface::TypeFlags type_flags = 0;"
    data = once(data, marker, marker + '''
#if XE_PLATFORM_PS5
  if (instance_extensions.ext_KHR_display) type_flags |= Surface::kTypeFlag_KhrDisplay;
#endif''')
    marker = "    switch (surface_type) {"
    data = once(data, marker, marker + '''
#if XE_PLATFORM_PS5
      case Surface::kTypeIndex_KhrDisplay: {
        const auto& display_surface = static_cast<const xbox360ps5::DisplaySurface&>(new_surface);
        VkExtent2D selected_extent{};
        uint32_t selected_refresh = 0;
        std::string error;
        if (!xbox360ps5::CreateDisplaySurface(ifn.vkGetInstanceProcAddr, instance,
              vulkan_device_->physical_device(), new_surface_width, new_surface_height,
              display_surface.refresh(), paint_context_.vulkan_surface,
              selected_extent, selected_refresh, error)) {
          XELOGE("PS5 KHR_display: {}", error);
          return SurfacePaintConnectResult::kFailure;
        }
        vulkan_surface_create_result = VK_SUCCESS;
      } break;
#endif''')
    put("vulkan_presenter.cc", data)
