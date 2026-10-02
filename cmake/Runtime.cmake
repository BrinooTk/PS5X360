# Actual engine components; no replacement Processor or guest kernel stubs.
set(XENIA_CPU_RUNTIME_NAMES
  breakpoint cpu_flags entry_table export_resolver function function_debug_info
  module processor raw_module test_module thread thread_state elf_module
  xex_module lzx mmio_handler stack_walker_posix)
set(XENIA_CPU_RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/guest_memory.cc")
foreach(name IN LISTS XENIA_CPU_RUNTIME_NAMES)
  list(APPEND XENIA_CPU_RUNTIME_SOURCES "${XENIA_SOURCE}/src/xenia/cpu/${name}.cc")
endforeach()
list(APPEND XENIA_CPU_RUNTIME_SOURCES
  "${XENIA_SOURCE}/src/xenia/cpu/backend/backend.cc"
  "${XENIA_SOURCE}/src/xenia/cpu/backend/assembler.cc")
add_library(xenia_cpu_runtime STATIC ${XENIA_CPU_RUNTIME_SOURCES})
target_link_libraries(xenia_cpu_runtime PUBLIC xenia_x64_backend xenia_cpu_config)
target_include_directories(xenia_cpu_runtime PRIVATE
  "${XENIA_SOURCE}/third_party/llvm/include"
  "${XENIA_SOURCE}/third_party/capstone/include")
target_compile_definitions(xenia_cpu_runtime PRIVATE NDEBUG
  XBYAK_NO_OP_NAMES XBYAK_ENABLE_OMITTED_OPERAND)
target_compile_options(xenia_cpu_runtime PRIVATE
  -ffunction-sections -fdata-sections -fno-char8_t -mavx)

add_library(xenia_platform_memory STATIC platform/ps5/memory.cpp)
target_link_libraries(xenia_platform_memory PUBLIC xenia_ppc_decoder)
target_compile_options(xenia_platform_memory PRIVATE -ffunction-sections -fdata-sections)
set(XENIA_BASE_RUNTIME_SOURCES platform/ps5/logging.cpp platform/ps5/thread_primitives.cpp)
if(CMAKE_CROSSCOMPILING)
  list(APPEND XENIA_BASE_RUNTIME_SOURCES platform/ps5/exception_handler.cpp)
else()
  list(APPEND XENIA_BASE_RUNTIME_SOURCES "${XENIA_SOURCE}/src/xenia/base/exception_handler_posix.cc")
endif()
foreach(name IN ITEMS byte_stream clock clock_x64 mutex string threading host_thread_context memory debugging_posix)
  list(APPEND XENIA_BASE_RUNTIME_SOURCES "${XENIA_SOURCE}/src/xenia/base/${name}.cc")
endforeach()
foreach(name IN ITEMS clock_posix filesystem_posix mapped_memory_posix)
  list(APPEND XENIA_BASE_RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/build/generated-sources/${name}.cc")
endforeach()
add_library(xenia_base_runtime STATIC ${XENIA_BASE_RUNTIME_SOURCES})
target_link_libraries(xenia_base_runtime PUBLIC xenia_cpu_config)
target_compile_definitions(xenia_base_runtime PRIVATE NDEBUG)
target_compile_options(xenia_base_runtime PRIVATE -ffunction-sections -fdata-sections -fno-char8_t -mavx)
add_executable(xenia-memory-contract src/memory_contract_test.cpp)
target_link_libraries(xenia-memory-contract PRIVATE xenia_platform_memory xenia_base_runtime)
if(NOT CMAKE_CROSSCOMPILING)
  target_link_libraries(xenia-memory-contract PRIVATE pthread)
  add_test(NAME guest_memory_contract COMMAND xenia-memory-contract)
endif()
add_executable(xenia-runtime-link src/runtime_link_gate.cpp)
target_link_libraries(xenia-runtime-link PRIVATE
  "-Wl,--start-group" xenia_cpu_runtime xenia_platform_memory xenia_base_runtime
  xenia_x64_backend xenia_cpu_compiler xenia_ppc_frontend xenia_hir_values
  xenia_cpu_config xenia_ppc_decoder xenia_capstone "-Wl,--end-group")
target_link_options(xenia-runtime-link PRIVATE -Wl,--gc-sections -Wl,--error-limit=0)
if(NOT CMAKE_CROSSCOMPILING)
  target_link_options(xenia-runtime-link PRIVATE -fuse-ld=lld -pthread)
  add_test(NAME actual_ppc_x64_execution COMMAND xenia-runtime-link)
  set_tests_properties(actual_ppc_x64_execution PROPERTIES TIMEOUT 25)
endif()
