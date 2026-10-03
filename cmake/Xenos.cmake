# Actual upstream Xenos translator, command processor, caches and Vulkan UI.
# These archives are compilation gates until the coordinator links them.
add_library(xenia_spirv_builder STATIC
  "${XENIA_SOURCE}/third_party/glslang/SPIRV/disassemble.cpp"
  "${XENIA_SOURCE}/third_party/glslang/SPIRV/doc.cpp"
  "${XENIA_SOURCE}/third_party/glslang/SPIRV/InReadableOrder.cpp"
  "${XENIA_SOURCE}/third_party/glslang/SPIRV/Logger.cpp"
  "${XENIA_SOURCE}/third_party/glslang/SPIRV/SpvBuilder.cpp"
  "${XENIA_SOURCE}/third_party/glslang/SPIRV/SPVRemapper.cpp")
target_compile_options(xenia_spirv_builder PRIVATE -ffunction-sections -fdata-sections)
add_library(xenia_xxhash STATIC "${XENIA_SOURCE}/third_party/xxhash/xxhash.c")
add_library(xenia_snappy STATIC "${XENIA_SOURCE}/third_party/snappy/snappy.cc"
  "${XENIA_SOURCE}/third_party/snappy/snappy-sinksource.cc"
  "${XENIA_SOURCE}/third_party/snappy/snappy-stubs-internal.cc")
file(GLOB XENOS_SOURCES CONFIGURE_DEPENDS "${XENIA_SOURCE}/src/xenia/gpu/*.cc")
list(FILTER XENOS_SOURCES EXCLUDE REGEX "/(dxbc_[^/]*|trace_(dump|player|reader|viewer)|shader_compiler_main|texture_dump|command_processor|spirv_shader_translator_fetch)\\.cc$")
list(APPEND XENOS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/command_processor.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/spirv_shader_translator_fetch.cc")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/command_processor.cc" APPEND PROPERTY
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include")
add_library(xenia_xenos_gpu STATIC ${XENOS_SOURCES})
target_link_libraries(xenia_xenos_gpu PUBLIC xenia_guest_kernel xenia_spirv_builder xenia_xxhash xenia_snappy)
file(GLOB VULKAN_RENDERER_SOURCES CONFIGURE_DEPENDS "${XENIA_SOURCE}/src/xenia/gpu/vulkan/*.cc")
list(FILTER VULKAN_RENDERER_SOURCES EXCLUDE REGEX "_main\\.cc$")
list(FILTER VULKAN_RENDERER_SOURCES EXCLUDE REGEX "/(vulkan_graphics_system|vulkan_command_processor)\\.cc$")
list(APPEND VULKAN_RENDERER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/vulkan_graphics_system.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/vulkan_command_processor.cc")
add_library(xenia_vulkan_renderer STATIC ${VULKAN_RENDERER_SOURCES})
target_link_libraries(xenia_vulkan_renderer PUBLIC xenia_xenos_gpu)
file(GLOB VULKAN_UI_SOURCES CONFIGURE_DEPENDS "${XENIA_SOURCE}/src/xenia/ui/vulkan/*.cc")
list(FILTER VULKAN_UI_SOURCES EXCLUDE REGEX "/(vulkan_window_demo|vulkan_instance|vulkan_presenter|spirv_tools_context)\\.cc$")
list(APPEND VULKAN_UI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/vulkan_instance.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/spirv_tools_context.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/vulkan_presenter.cc"
  "${XENIA_SOURCE}/src/xenia/ui/renderdoc_api.cc")
if(CMAKE_CROSSCOMPILING)
  list(APPEND VULKAN_UI_SOURCES platform/ps5/radv_dispatch.cpp platform/ps5/vulkan_platform.cpp)
endif()

add_executable(xenia-shader-contract src/shader_contract.cpp)
target_link_libraries(xenia-shader-contract PRIVATE xenia_xenos_gpu xenia_vulkan_ui)
target_link_options(xenia-shader-contract PRIVATE -Wl,--gc-sections)
if(NOT CMAKE_CROSSCOMPILING)
  target_link_libraries(xenia-shader-contract PRIVATE dl pthread)
  add_test(NAME actual_xenos_spirv_translation COMMAND xenia-shader-contract)
  set_tests_properties(actual_xenos_spirv_translation PROPERTIES TIMEOUT 30)
endif()
add_library(xenia_vulkan_ui STATIC ${VULKAN_UI_SOURCES})
target_link_libraries(xenia_vulkan_ui PUBLIC xenia_base_runtime)
foreach(target IN ITEMS xenia_xenos_gpu xenia_vulkan_renderer xenia_vulkan_ui)
  target_include_directories(${target} PUBLIC include "${XENIA_SOURCE}/third_party/Vulkan-Headers/include")
  target_compile_definitions(${target} PRIVATE NDEBUG)
  target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections -fno-char8_t -mavx)
endforeach()

add_executable(xenia-vulkan-device-contract src/vulkan_device_contract.cpp)
target_link_libraries(xenia-vulkan-device-contract PRIVATE xenia_vulkan_ui)
target_link_options(xenia-vulkan-device-contract PRIVATE -Wl,--gc-sections)
if(NOT CMAKE_CROSSCOMPILING)
  target_link_libraries(xenia-vulkan-device-contract PRIVATE dl pthread)
  add_test(NAME actual_xenia_vulkan_device COMMAND xenia-vulkan-device-contract)
  set_tests_properties(actual_xenia_vulkan_device PROPERTIES TIMEOUT 30)
endif()
