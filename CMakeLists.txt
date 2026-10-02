cmake_minimum_required(VERSION 3.20)
project(Xbox360PS5 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(XENIA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/.deps/xenia")
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

if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_test(NAME xenon_opcode_decoder COMMAND xenia-platform-smoke)
  add_test(NAME xenon_hir_semantics COMMAND xenia-hir-smoke)
endif()
