set(VFS_SOURCES
  "${XENIA_SOURCE}/src/xenia/vfs/device.cc"
  "${XENIA_SOURCE}/src/xenia/vfs/entry.cc"
  "${XENIA_SOURCE}/src/xenia/vfs/virtual_file_system.cc"
  "${XENIA_SOURCE}/src/xenia/vfs/devices/host_path_device.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/host_path_entry.cc"
  "${XENIA_SOURCE}/src/xenia/vfs/devices/host_path_file.cc"
  "${XENIA_SOURCE}/src/xenia/base/filesystem.cc"
  "${XENIA_SOURCE}/src/xenia/base/filesystem_wildcard.cc")
add_library(xenia_guest_vfs STATIC ${VFS_SOURCES})
target_link_libraries(xenia_guest_vfs PUBLIC xenia_base_runtime xenia_platform_memory)
target_compile_definitions(xenia_guest_vfs PRIVATE NDEBUG)
target_compile_options(xenia_guest_vfs PRIVATE -ffunction-sections -fdata-sections -fno-char8_t)
add_executable(xenia-vfs-contract src/vfs_contract.cpp)
target_link_libraries(xenia-vfs-contract PRIVATE xenia_guest_vfs)
target_link_options(xenia-vfs-contract PRIVATE -Wl,--gc-sections)
if(NOT CMAKE_CROSSCOMPILING)
  target_link_libraries(xenia-vfs-contract PRIVATE pthread)
  add_test(NAME actual_guest_vfs COMMAND xenia-vfs-contract)
  set_tests_properties(actual_guest_vfs PROPERTIES TIMEOUT 30)
endif()

add_executable(xenia-thread-contract src/thread_contract.cpp)
target_link_libraries(xenia-thread-contract PRIVATE xenia_base_runtime)
target_link_options(xenia-thread-contract PRIVATE -Wl,--gc-sections)
if(NOT CMAKE_CROSSCOMPILING)
  target_link_libraries(xenia-thread-contract PRIVATE pthread)
  add_test(NAME actual_thread_synchronization COMMAND xenia-thread-contract)
  set_tests_properties(actual_thread_synchronization PROPERTIES TIMEOUT 30)
endif()

# Compile the actual xam/xboxkrnl modules and kernel objects. This archive is
# not a linked KernelState until the coordinator and scheduler are connected.
file(GLOB_RECURSE KERNEL_SOURCES CONFIGURE_DEPENDS "${XENIA_SOURCE}/src/xenia/kernel/*.cc")
list(FILTER KERNEL_SOURCES EXCLUDE REGEX "(_win|_test|_linux|_android)\\.cc$")
list(FILTER KERNEL_SOURCES EXCLUDE REGEX "/(xam_net|xsocket)\\.cc$")
list(APPEND KERNEL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/xam_net.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/xsocket.cc")
add_library(xenia_guest_kernel STATIC ${KERNEL_SOURCES})
target_link_libraries(xenia_guest_kernel PUBLIC xenia_cpu_runtime xenia_guest_vfs)
target_include_directories(xenia_guest_kernel PRIVATE "${XENIA_SOURCE}/third_party/llvm/include")
target_compile_definitions(xenia_guest_kernel PRIVATE NDEBUG)
target_compile_options(xenia_guest_kernel PRIVATE -ffunction-sections -fdata-sections -fno-char8_t -mavx)

# The real coordinator and input router are separate from the kernel archive.
# Linking and booting still requires the renderer, audio and native application.
add_library(xenia_emulator_coordinator STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/emulator.cc" "${XENIA_SOURCE}/src/xenia/config.cc"
  "${XENIA_SOURCE}/src/xenia/hid/input_system.cc"
  "${XENIA_SOURCE}/src/xenia/hid/hid_flags.cc")
target_link_libraries(xenia_emulator_coordinator PUBLIC xenia_guest_kernel)
target_include_directories(xenia_emulator_coordinator PRIVATE "${XENIA_SOURCE}/src/xenia")
target_compile_definitions(xenia_emulator_coordinator PRIVATE NDEBUG)
target_compile_options(xenia_emulator_coordinator PRIVATE -ffunction-sections -fdata-sections -fno-char8_t -mavx)

add_library(xenia_dualsense_input STATIC platform/ps5/dualsense_input.cpp)
target_link_libraries(xenia_dualsense_input PUBLIC xenia_emulator_coordinator)
target_include_directories(xenia_dualsense_input PUBLIC include)
target_compile_options(xenia_dualsense_input PRIVATE -ffunction-sections -fdata-sections -fno-char8_t)
add_executable(xenia-input-contract src/input_contract.cpp)
target_link_libraries(xenia-input-contract PRIVATE xenia_dualsense_input)
target_link_options(xenia-input-contract PRIVATE -Wl,--gc-sections)
if(NOT CMAKE_CROSSCOMPILING)
  target_link_libraries(xenia-input-contract PRIVATE pthread)
  add_test(NAME actual_guest_input_router COMMAND xenia-input-contract)
  set_tests_properties(actual_guest_input_router PROPERTIES TIMEOUT 30)
endif()
