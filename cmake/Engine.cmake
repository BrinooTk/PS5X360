# Link the real coordinator, kernel, CPU, audio and renderer together.
file(GLOB EXTRA_VFS CONFIGURE_DEPENDS "${XENIA_SOURCE}/src/xenia/vfs/devices/*.cc")
list(FILTER EXTRA_VFS EXCLUDE REGEX "/host_path_[^/]*\\.cc$")
target_sources(xenia_guest_vfs PRIVATE ${EXTRA_VFS})
target_sources(xenia_cpu_runtime PRIVATE "${XENIA_SOURCE}/src/xenia/cpu/backend/null_backend.cc")
target_sources(xenia_base_runtime PRIVATE
  "${XENIA_SOURCE}/src/xenia/base/bit_map.cc"
  "${XENIA_SOURCE}/src/xenia/base/bit_stream.cc"
  "${XENIA_SOURCE}/src/xenia/base/ring_buffer.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/profiling.cc")
add_library(xenia_xex_support STATIC
  "${XENIA_SOURCE}/third_party/aes_128/unroll/aes.c"
  "${XENIA_SOURCE}/third_party/mspack/lzxd.c"
  "${XENIA_SOURCE}/third_party/mspack/system.c"
  "${XENIA_SOURCE}/third_party/mspack/logging.cc")
target_include_directories(xenia_xex_support PRIVATE "${XENIA_SOURCE}/third_party/mspack" "${XENIA_SOURCE}/third_party/aes_128")
target_compile_definitions(xenia_xex_support PRIVATE HAVE_CONFIG_H NDEBUG)
target_link_libraries(xenia_xex_support PUBLIC xenia_base_runtime)
add_library(xenia_ui_core STATIC
  "${XENIA_SOURCE}/src/xenia/ui/window.cc"
  "${XENIA_SOURCE}/src/xenia/ui/windowed_app_context.cc"
  "${XENIA_SOURCE}/src/xenia/ui/menu_item.cc"
  "${XENIA_SOURCE}/src/xenia/ui/imgui_dialog.cc"
  "${XENIA_SOURCE}/src/xenia/ui/imgui_drawer.cc"
  "${XENIA_SOURCE}/src/xenia/ui/immediate_drawer.cc"
  "${XENIA_SOURCE}/src/xenia/ui/graphics_util.cc"
  "${XENIA_SOURCE}/src/xenia/ui/graphics_upload_buffer_pool.cc"
  "${XENIA_SOURCE}/src/xenia/ui/presenter.cc"
  "${XENIA_SOURCE}/third_party/imgui/imgui.cpp"
  "${XENIA_SOURCE}/third_party/imgui/imgui_draw.cpp"
  "${XENIA_SOURCE}/third_party/imgui/imgui_widgets.cpp"
  "${XENIA_SOURCE}/third_party/imgui/imgui_tables.cpp")
target_link_libraries(xenia_ui_core PUBLIC xenia_base_runtime)
target_compile_options(xenia_ui_core PRIVATE -ffunction-sections -fdata-sections -fno-char8_t)
target_compile_definitions(xenia_ui_core PRIVATE NDEBUG)
if(NOT CMAKE_CROSSCOMPILING)
add_executable(xenia-engine-integration src/engine_integration.cpp)
target_link_libraries(xenia-engine-integration PRIVATE "-Wl,--start-group"
  xenia_emulator_coordinator xenia_guest_kernel xenia_vulkan_renderer
  xenia_xenos_gpu xenia_vulkan_ui xenia_ui_core xenia_audio_engine
  xenia_dualsense_input xenia_cpu_runtime xenia_platform_memory xenia_base_runtime
  xenia_ppc_frontend xenia_xex_support
  "-Wl,--end-group")
target_link_options(xenia-engine-integration PRIVATE -Wl,--gc-sections -Wl,--error-limit=0)
  target_link_libraries(xenia-engine-integration PRIVATE dl pthread m)
  target_link_options(xenia-engine-integration PRIVATE -fuse-ld=lld)
  add_test(NAME actual_engine_lifecycle COMMAND xenia-engine-integration)
  set_tests_properties(actual_engine_lifecycle PROPERTIES TIMEOUT 60)
else()
  # Final linking uses the native title CRT, not the payload SDK's startup.
  add_library(xenia-native-game-objects OBJECT src/native_game_main.cpp
    platform/ps5/native_memory_calls.cpp platform/ps5/libc_bits.c)
  target_link_libraries(xenia-native-game-objects PRIVATE xenia_dualsense_input
    xenia_vulkan_renderer xenia_audio_engine xenia_ui_core xenia_xex_support)
  target_compile_options(xenia-native-game-objects PRIVATE -fno-char8_t -mavx)
endif()
# Existing compiler depfiles may point to upstream thread.h from before the
# overlay existed. Explicitly invalidate those objects on this new header.
foreach(target IN ITEMS xenia_cpu_runtime xenia_ppc_frontend xenia_x64_backend
    xenia_guest_kernel xenia_emulator_coordinator xenia_audio_engine xenia_xenos_gpu
    xenia_vulkan_renderer xenia_dualsense_input)
  get_target_property(tls_sources ${target} SOURCES)
  set_property(SOURCE ${tls_sources} APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/build/generated/xenia/cpu/thread.h")
endforeach()
