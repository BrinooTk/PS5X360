# Xbox360PS5 on the Xenia Canary core.
#
# Canary's own build (../.deps/xenia-canary, with the patches from
# ../patches/canary applied by tools/prepare_canary.py) compiles the emulator;
# this project adds the PS5 parts on top of its libraries. On the PC it builds
# the test runner (xbox360ps5-runner): a game headless, with a virtual
# controller and screenshots of the guest output.
cmake_minimum_required(VERSION 3.20)
project(xbox360ps5_canary LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(XBOX360PS5_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(XENIA_CANARY "${XBOX360PS5_ROOT}/.deps/xenia-canary")
set(XBOX360PS5_VERSION_OVERRIDE "" CACHE STRING "Version label for a separate experimental build")
option(XBOX360PS5_EXPERIMENTAL_DIAGNOSTICS "Enable experimental GPU diagnostics and cache guards" OFF)
if(XBOX360PS5_VERSION_OVERRIDE)
  set(XBOX360PS5_VERSION "${XBOX360PS5_VERSION_OVERRIDE}")
else()
  file(STRINGS "${XBOX360PS5_ROOT}/VERSION" XBOX360PS5_VERSION LIMIT_COUNT 1)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${XBOX360PS5_ROOT}/VERSION")

if(CMAKE_CROSSCOMPILING)
  # Headers Linux has and the console's FreeBSD-derived SDK does not.
  include_directories(BEFORE SYSTEM "${XBOX360PS5_ROOT}/platform/ps5/compat")
  # Canary's PS5 branches (Vulkan loader, video output) use the platform's headers.
  include_directories("${XBOX360PS5_ROOT}/include")
  add_compile_options(-march=znver2 -fno-stack-protector)
  # The title's module converter takes weak references only from Mesa: zstd's
  # optional tracing hooks are weak.
  add_compile_definitions(ZSTD_TRACE=0)
endif()

add_subdirectory("${XENIA_CANARY}" core EXCLUDE_FROM_ALL)
target_include_directories(xenia-kernel PRIVATE "${XBOX360PS5_ROOT}/include")

target_include_directories(xenia-base PRIVATE "${XBOX360PS5_ROOT}/include")
target_include_directories(xenia-vfs PRIVATE "${XBOX360PS5_ROOT}/include")

set(LIBSMB2_SOURCE "${XBOX360PS5_ROOT}/.deps/libsmb2")
set(LIBSMB2_SOURCES
  "${LIBSMB2_SOURCE}/lib/aes.c"
  "${LIBSMB2_SOURCE}/lib/aes128ccm.c"
  "${LIBSMB2_SOURCE}/lib/alloc.c"
  "${LIBSMB2_SOURCE}/lib/asn1-ber.c"
  "${LIBSMB2_SOURCE}/lib/compat.c"
  "${LIBSMB2_SOURCE}/lib/dcerpc.c"
  "${LIBSMB2_SOURCE}/lib/dcerpc-lsa.c"
  "${LIBSMB2_SOURCE}/lib/dcerpc-srvsvc.c"
  "${LIBSMB2_SOURCE}/lib/errors.c"
  "${LIBSMB2_SOURCE}/lib/hmac.c"
  "${LIBSMB2_SOURCE}/lib/hmac-md5.c"
  "${LIBSMB2_SOURCE}/lib/init.c"
  "${LIBSMB2_SOURCE}/lib/krb5-wrapper.c"
  "${LIBSMB2_SOURCE}/lib/libsmb2.c"
  "${LIBSMB2_SOURCE}/lib/md4c.c"
  "${LIBSMB2_SOURCE}/lib/md5.c"
  "${LIBSMB2_SOURCE}/lib/ntlmssp.c"
  "${LIBSMB2_SOURCE}/lib/pdu.c"
  "${LIBSMB2_SOURCE}/lib/sha1.c"
  "${LIBSMB2_SOURCE}/lib/sha224-256.c"
  "${LIBSMB2_SOURCE}/lib/sha384-512.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-close.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-create.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-echo.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-error.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-flush.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-ioctl.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-lock.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-logoff.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-negotiate.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-notify-change.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-oplock-break.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-query-directory.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-query-info.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-read.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-session-setup.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-set-info.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-tree-connect.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-tree-disconnect.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-write.c"
  "${LIBSMB2_SOURCE}/lib/smb2-data-file-info.c"
  "${LIBSMB2_SOURCE}/lib/smb2-data-filesystem-info.c"
  "${LIBSMB2_SOURCE}/lib/smb2-data-reparse-point.c"
  "${LIBSMB2_SOURCE}/lib/smb2-data-security-descriptor.c"
  "${LIBSMB2_SOURCE}/lib/smb2-share-enum.c"
  "${LIBSMB2_SOURCE}/lib/smb2-signing.c"
  "${LIBSMB2_SOURCE}/lib/smb3-seal.c"
  "${LIBSMB2_SOURCE}/lib/socket.c"
  "${LIBSMB2_SOURCE}/lib/spnego-wrapper.c"
  "${LIBSMB2_SOURCE}/lib/sync.c"
  "${LIBSMB2_SOURCE}/lib/timestamps.c"
  "${LIBSMB2_SOURCE}/lib/unicode.c"
  "${LIBSMB2_SOURCE}/lib/usha.c")

add_library(xenia_libsmb2 STATIC ${LIBSMB2_SOURCES})
target_include_directories(xenia_libsmb2 PUBLIC
  "${LIBSMB2_SOURCE}/include"
  "${LIBSMB2_SOURCE}/include/smb2")
target_compile_definitions(xenia_libsmb2 PRIVATE
  NDEBUG
  HAVE_CONFIG_H=0
  _U_=__attribute__\(\(unused\)\))
target_compile_options(xenia_libsmb2 PRIVATE
  -ffunction-sections -fdata-sections -Wno-unused-parameter)


set(XBOX360PS5_CANARY_LIBRARIES
  xenia-apu xenia-apu-nop xenia-base xenia-core xenia-cpu xenia-gpu xenia-hid
  xenia-kernel xenia-patcher xenia-ui xenia-vfs xenia-gpu-vulkan xenia-ui-vulkan
  xenia-cpu-backend-x64 xenia_libsmb2
  aes_128 capstone fmt dxbc imgui libavcodec libavutil mspack snappy xxhash
  glslang-spirv)

if(NOT CMAKE_CROSSCOMPILING)
  add_executable(xbox360ps5-runner
    "${XBOX360PS5_ROOT}/src/canary_runner.cpp"
    "${XBOX360PS5_ROOT}/src/game_patches.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/dualsense_input.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/canary_audio.cpp")
  target_include_directories(xbox360ps5-runner PRIVATE
    "${XBOX360PS5_ROOT}/include" "${XENIA_CANARY}" "${XENIA_CANARY}/src"
    "${XENIA_CANARY}/third_party" "${XENIA_CANARY}/third_party/fmt/include"
    "${XENIA_CANARY}/third_party/Vulkan-Headers/include")
  target_compile_definitions(xbox360ps5-runner PRIVATE XBOX360PS5_CANARY=1)
  target_compile_options(xbox360ps5-runner PRIVATE -mavx)
  target_link_libraries(xbox360ps5-runner PRIVATE
    -Wl,--start-group ${XBOX360PS5_CANARY_LIBRARIES} -Wl,--end-group
    stdc++fs dl lz4 pthread rt)
  # Canary's libraries are LLVM bitcode (thin LTO): link with lld like Canary does.
  target_link_options(xbox360ps5-runner PRIVATE -fuse-ld=lld -flto=thin)
  # Offline visual check of the actual launcher draw list, without a GPU or ROM.
  add_executable(xbox360ps5-launcher-preview EXCLUDE_FROM_ALL
    "${XBOX360PS5_ROOT}/tools/preview-launcher.cpp"
    "${XBOX360PS5_ROOT}/src/launcher.cpp"
    "${XBOX360PS5_ROOT}/src/covers.cpp"
    "${XBOX360PS5_ROOT}/src/game_patches.cpp")
  target_include_directories(xbox360ps5-launcher-preview PRIVATE
    "${XBOX360PS5_ROOT}/include" "${XENIA_CANARY}" "${XENIA_CANARY}/src"
    "${XENIA_CANARY}/third_party" "${XENIA_CANARY}/third_party/fmt/include"
    "${XENIA_CANARY}/third_party/Vulkan-Headers/include")
  target_compile_definitions(xbox360ps5-launcher-preview PRIVATE XBOX360PS5_CANARY=1
    XBOX360PS5_VERSION="${XBOX360PS5_VERSION}")
  target_compile_options(xbox360ps5-launcher-preview PRIVATE -mavx)
  target_link_libraries(xbox360ps5-launcher-preview PRIVATE
    -Wl,--start-group ${XBOX360PS5_CANARY_LIBRARIES} -Wl,--end-group
    stdc++fs dl lz4 pthread rt)
  target_link_options(xbox360ps5-launcher-preview PRIVATE -fuse-ld=lld -flto=thin)
endif()

if(CMAKE_CROSSCOMPILING)
  # The title: linked by tools/build-canary-game.sh with the native title CRT
  # and the RADV driver, not by CMake.
  add_library(xbox360ps5-title-objects OBJECT
    "${XBOX360PS5_ROOT}/src/canary_game_main.cpp"
    "${XBOX360PS5_ROOT}/src/launcher.cpp"
    "${XBOX360PS5_ROOT}/src/covers.cpp"
    "${XBOX360PS5_ROOT}/src/autotest.cpp"
    "${XBOX360PS5_ROOT}/src/game_patches.cpp"
	"${XBOX360PS5_ROOT}/src/smb_path_device.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/memory_ps5.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/exception_handler.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/dualsense_input.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/canary_audio.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/ui_sounds.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/canary_system.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/crash_report.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/thread_place.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/notify.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/web_settings.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/net_log.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/libc_bits.c"
    "${XBOX360PS5_ROOT}/platform/ps5/native_memory_calls.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/radv_dispatch.cpp"
    "${XBOX360PS5_ROOT}/platform/ps5/vulkan_platform.cpp"
	"${XBOX360PS5_ROOT}/platform/ps5/smb_client.cpp")
  target_include_directories(xbox360ps5-title-objects PRIVATE
    "${XBOX360PS5_ROOT}/include" "${XENIA_CANARY}" "${XENIA_CANARY}/src"
    "${XENIA_CANARY}/third_party" "${XENIA_CANARY}/third_party/fmt/include"
    "${XENIA_CANARY}/third_party/Vulkan-Headers/include"
	"${LIBSMB2_SOURCE}/include"
	"${LIBSMB2_SOURCE}/include/smb2")
  target_compile_definitions(xbox360ps5-title-objects PRIVATE XBOX360PS5_CANARY=1 NDEBUG
    XBOX360PS5_VERSION="${XBOX360PS5_VERSION}")
  if(XBOX360PS5_EXPERIMENTAL_DIAGNOSTICS)
    target_compile_definitions(xbox360ps5-title-objects PRIVATE XBOX360PS5_EXPERIMENTAL_DIAGNOSTICS=1)
  endif()
  target_compile_options(xbox360ps5-title-objects PRIVATE -O2 -mavx)
endif()
