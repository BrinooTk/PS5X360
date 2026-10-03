cmake_minimum_required(VERSION 3.20)
project(Xbox360PS5 LANGUAGES C CXX)
option(XBOX360PS5_BUILD_RUNTIME "Compile full CPU, guest memory and executable loaders" OFF)
option(XBOX360PS5_BUILD_VULKAN_PLATFORM "Build the driver-neutral Vulkan platform boundary" OFF)
option(XBOX360PS5_BUILD_KERNEL "Build real guest kernel and VFS components" OFF)
option(XBOX360PS5_BUILD_XENOS "Build the actual Xenos Vulkan renderer" OFF)
option(XBOX360PS5_BUILD_AUDIO "Build actual XMA decoder and audio engine" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The core (Xenia Canary, or the original Xenia) as tools/prepare.py chose it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/generated/xenia_base.cmake")
  include("${CMAKE_CURRENT_SOURCE_DIR}/build/generated/xenia_base.cmake")
else()
  set(XENIA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/.deps/xenia")
  set(XBOX360PS5_CANARY OFF)
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/generated/xenia/base/platform.h")
  message(FATAL_ERROR "Run python3 tools/prepare.py first")
endif()

# First real engine component: the upstream Xenon/VMX128 instruction decoder.
add_library(xenia_ppc_decoder STATIC
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_opcode_lookup_gen.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_opcode_info.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_opcode_table_gen.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_opcode_disasm.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_opcode_disasm_gen.cc"
  "${XENIA_SOURCE}/src/xenia/base/string_buffer.cc"
  "${XENIA_SOURCE}/third_party/fmt/src/format.cc")
target_include_directories(xenia_ppc_decoder PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/build/generated"
  "${XENIA_SOURCE}/src" "${XENIA_SOURCE}"
  "${XENIA_SOURCE}/third_party/fmt/include")
target_compile_definitions(xenia_ppc_decoder PRIVATE NDEBUG)
target_compile_options(xenia_ppc_decoder PRIVATE -Wall -Wextra)

add_executable(xenia-platform-smoke src/platform_smoke.cpp)
target_include_directories(xenia-platform-smoke PRIVATE include)
target_link_libraries(xenia-platform-smoke PRIVATE xenia_ppc_decoder)

# Actual upstream constant evaluator used by the CPU translation pipeline.
add_library(xenia_hir_values STATIC
  "${XENIA_SOURCE}/src/xenia/base/arena.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/hir/hir_builder.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/hir/block.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/hir/instr.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/hir/opcodes.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/hir/value.cc")
target_link_libraries(xenia_hir_values PUBLIC xenia_ppc_decoder)
target_compile_definitions(xenia_hir_values PRIVATE NDEBUG)
add_executable(xenia-hir-smoke src/hir_smoke.cpp)
target_link_libraries(xenia-hir-smoke PRIVATE xenia_hir_values)

# Compilation gate only: runtime dependencies remain unresolved in this archive.
add_library(xenia_ppc_frontend STATIC
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_frontend.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_scanner.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_translator.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_hir_builder.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_emit_alu.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_emit_altivec.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_emit_control.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_emit_fpu.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/ppc/ppc_emit_memory.cc")
target_link_libraries(xenia_ppc_frontend PUBLIC xenia_hir_values)
target_include_directories(xenia_ppc_frontend PUBLIC "${XENIA_SOURCE}/third_party/llvm/include")
target_compile_definitions(xenia_ppc_frontend PRIVATE NDEBUG)
target_compile_options(xenia_ppc_frontend PRIVATE -ffunction-sections -fdata-sections)

add_library(xenia_cpu_config STATIC
  "${XENIA_SOURCE}/src/xenia/base/cvar.cc"
  "${XENIA_SOURCE}/src/xenia/base/utf8.cc")
target_link_libraries(xenia_cpu_config PUBLIC xenia_ppc_decoder)
target_compile_definitions(xenia_cpu_config PRIVATE NDEBUG)
target_compile_options(xenia_cpu_config PRIVATE -ffunction-sections -fdata-sections -fno-char8_t)
add_executable(xenia-ppc-translation-smoke src/ppc_translation_smoke.cpp)
target_link_libraries(xenia-ppc-translation-smoke PRIVATE xenia_ppc_frontend xenia_cpu_config)
target_link_options(xenia-ppc-translation-smoke PRIVATE -Wl,--gc-sections)

file(GLOB XENIA_COMPILER_SOURCES CONFIGURE_DEPENDS
  "${XENIA_SOURCE}/src/xenia/cpu/compiler/*.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/compiler/passes/*.cc")
add_library(xenia_cpu_compiler STATIC ${XENIA_COMPILER_SOURCES})
target_link_libraries(xenia_cpu_compiler PUBLIC xenia_hir_values)
target_include_directories(xenia_cpu_compiler PUBLIC "${XENIA_SOURCE}/third_party/llvm/include")
target_compile_definitions(xenia_cpu_compiler PRIVATE NDEBUG)
add_executable(xenia-compiler-smoke src/compiler_smoke.cpp)
target_link_libraries(xenia-compiler-smoke PRIVATE xenia_cpu_compiler)
target_link_libraries(xenia-ppc-translation-smoke PRIVATE xenia_cpu_compiler)

file(GLOB XENIA_X64_SOURCES CONFIGURE_DEPENDS
  "${XENIA_SOURCE}/src/xenia/cpu/backend/x64/*.cc")
list(FILTER XENIA_X64_SOURCES EXCLUDE REGEX "_win\\.cc$")
foreach(name IN ITEMS x64_backend x64_emitter x64_sequences x64_seq_vector x64_tracers x64_code_cache)
  list(REMOVE_ITEM XENIA_X64_SOURCES "${XENIA_SOURCE}/src/xenia/cpu/backend/x64/${name}.cc")
  list(APPEND XENIA_X64_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/xenia/cpu/backend/x64/${name}.cc")
endforeach()
# Explicit dependencies also invalidate objects built before an overlay header
# first existed (compiler depfiles otherwise still point to upstream headers).
set_property(SOURCE ${XENIA_X64_SOURCES} APPEND PROPERTY OBJECT_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/build/generated/xenia/cpu/backend/x64/x64_stack_layout.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/build/generated/xenia/cpu/backend/x64/x64_code_cache.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/build/generated/xenia/cpu/backend/x64/x64_tracers.h")
add_library(xenia_x64_backend STATIC ${XENIA_X64_SOURCES})
target_link_libraries(xenia_x64_backend PUBLIC xenia_cpu_compiler)
target_include_directories(xenia_x64_backend PRIVATE "${XENIA_SOURCE}/third_party/capstone/include")
target_compile_definitions(xenia_x64_backend PRIVATE NDEBUG
  CAPSTONE_X86_ATT_DISABLE CAPSTONE_DIET_NO CAPSTONE_X86_REDUCE_NO
  CAPSTONE_HAS_X86 CAPSTONE_USE_SYS_DYN_MEM
  XBYAK_NO_OP_NAMES XBYAK_ENABLE_OMITTED_OPERAND)

set(CAPSTONE_SOURCE "${XENIA_SOURCE}/third_party/capstone")
add_library(xenia_capstone STATIC
  "${CAPSTONE_SOURCE}/cs.c" "${CAPSTONE_SOURCE}/MCInst.c"
  "${CAPSTONE_SOURCE}/MCInstrDesc.c" "${CAPSTONE_SOURCE}/MCRegisterInfo.c"
  "${CAPSTONE_SOURCE}/SStream.c" "${CAPSTONE_SOURCE}/utils.c"
  "${CAPSTONE_SOURCE}/arch/X86/X86Disassembler.c"
  "${CAPSTONE_SOURCE}/arch/X86/X86DisassemblerDecoder.c"
  "${CAPSTONE_SOURCE}/arch/X86/X86IntelInstPrinter.c"
  "${CAPSTONE_SOURCE}/arch/X86/X86Mapping.c"
  "${CAPSTONE_SOURCE}/arch/X86/X86Module.c")
target_include_directories(xenia_capstone PUBLIC "${CAPSTONE_SOURCE}/include")
target_compile_definitions(xenia_capstone PRIVATE CAPSTONE_HAS_X86
  CAPSTONE_USE_SYS_DYN_MEM CAPSTONE_X86_ATT_DISABLE)
target_link_libraries(xenia_x64_backend PUBLIC xenia_capstone)
add_executable(xenia-codegen-smoke src/codegen_smoke.cpp)
target_include_directories(xenia-codegen-smoke PRIVATE "${XENIA_SOURCE}")
target_compile_definitions(xenia-codegen-smoke PRIVATE XBYAK_NO_OP_NAMES XBYAK_ENABLE_OMITTED_OPERAND)
target_link_libraries(xenia-codegen-smoke PRIVATE xenia_capstone)

add_executable(xenia-atomic-smoke src/atomic_smoke.cpp)
target_link_libraries(xenia-atomic-smoke PRIVATE xenia_ppc_decoder)
# Exercise the PS5 branch on the host as well as with the target SDK.
target_compile_definitions(xenia-atomic-smoke PRIVATE XE_PLATFORM_PS5=1)
if(NOT CMAKE_CROSSCOMPILING)
  target_link_libraries(xenia-atomic-smoke PRIVATE pthread)
endif()

if(XBOX360PS5_BUILD_RUNTIME)
  include(cmake/Runtime.cmake)
endif()

if(XBOX360PS5_BUILD_VULKAN_PLATFORM)
  include(cmake/VulkanPlatform.cmake)
endif()
if(XBOX360PS5_BUILD_KERNEL)
  if(NOT XBOX360PS5_BUILD_RUNTIME)
    message(FATAL_ERROR "The kernel requires the actual CPU runtime")
  endif()
  include(cmake/Kernel.cmake)
endif()
if(XBOX360PS5_BUILD_XENOS)
  if(NOT XBOX360PS5_BUILD_KERNEL)
    message(FATAL_ERROR "Xenos requires the kernel and runtime components")
  endif()
  include(cmake/Xenos.cmake)
endif()
if(XBOX360PS5_BUILD_AUDIO)
  if(NOT XBOX360PS5_BUILD_KERNEL)
    message(FATAL_ERROR "Audio requires the kernel and full threading runtime")
  endif()
  include(cmake/Audio.cmake)
endif()

if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_test(NAME xenon_opcode_decoder COMMAND xenia-platform-smoke)
  add_test(NAME xenon_hir_semantics COMMAND xenia-hir-smoke)
  add_test(NAME ps5_atomic_semantics COMMAND xenia-atomic-smoke)
  add_test(NAME xenon_optimizer_semantics COMMAND xenia-compiler-smoke)
  add_test(NAME x64_codegen_decode COMMAND xenia-codegen-smoke)
  add_test(NAME ppc_instruction_translation COMMAND xenia-ppc-translation-smoke)
endif()
if(XBOX360PS5_BUILD_AUDIO AND XBOX360PS5_BUILD_XENOS)
  include(cmake/Engine.cmake)
endif()
