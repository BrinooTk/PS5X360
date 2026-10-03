"""Fetch pinned inputs and generate a PS5 platform overlay without editing Xenia."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import re

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    # The emulator core: Xenia Canary (XBOX360PS5_BASE=canary), or the original
    # Xenia project the port started from.
    canary = os.environ.get("XBOX360PS5_BASE", "xenia") == "canary"
    dep = json.loads((ROOT / "deps.json").read_text())["xenia_canary" if canary else "xenia"]
    source = ROOT / (".deps/xenia-canary" if canary else ".deps/xenia")
    if args.fetch:
        if not source.exists():
            subprocess.check_call(["git", "clone", "--no-checkout", dep["url"], str(source)])
        if run("git", "-C", str(source), "status", "--porcelain"):
            raise SystemExit("Xenia has local changes; refusing checkout")
        if subprocess.run(["git", "-C", str(source), "cat-file", "-e", dep["revision"]],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            subprocess.check_call(["git", "-C", str(source), "fetch", "origin", dep["revision"]])
        subprocess.check_call(["git", "-C", str(source), "checkout", "--detach", dep["revision"]])
        if canary:
            paths = run("git", "-C", str(source), "config", "-f", ".gitmodules", "--get-regexp", "path").split("\n")
            submodules = [line.split()[1] for line in paths if line and line.split()[1] not in dep["submodules_excluded"]]
        else:
            submodules = dep["submodules"]
        subprocess.check_call(["git", "-C", str(source), "submodule", "update", "--init",
                               "--depth", "1", *submodules])
    actual = run("git", "-C", str(source), "rev-parse", "HEAD")
    if actual != dep["revision"]:
        raise SystemExit(f"Wrong Xenia revision: {actual}")
    # Which core the CMake build compiles (read by CMakeLists.txt).
    target = ROOT / "build/generated/xenia_base.cmake"
    target.parent.mkdir(parents=True, exist_ok=True)
    text = (f'set(XENIA_SOURCE "${{CMAKE_CURRENT_SOURCE_DIR}}/{source.relative_to(ROOT).as_posix()}")\n'
            f"set(XBOX360PS5_CANARY {'ON' if canary else 'OFF'})\n")
    if not target.exists() or target.read_text() != text:
        target.write_text(text)
    platform = (source / "src/xenia/base/platform.h").read_text()
    marker = "#if defined(TARGET_OS_MAC) && TARGET_OS_MAC"
    if platform.count(marker) != 1:
        raise SystemExit("Platform overlay anchor changed")
    platform = platform.replace(marker,
        "#if defined(__PROSPERO__)\n#define XE_PLATFORM_PS5 1\n#elif defined(TARGET_OS_MAC) && TARGET_OS_MAC")
    target = ROOT / "build/generated/xenia/base/platform.h"
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_text() != platform:
        target.write_text(platform)
    # Canary's generic atomics (GCC builtins) already suit the PS5 toolchain.
    target = ROOT / "build/generated/xenia/base/atomic.h"
    if canary:
        target.unlink(missing_ok=True)
    else:
        atomic = (source / "src/xenia/base/atomic.h").read_text()
        marker = "#elif XE_PLATFORM_LINUX || XE_PLATFORM_MAC"
        if atomic.count(marker) != 1:
            raise SystemExit("Atomic overlay anchor changed")
        primitives = (ROOT / "platform/ps5/atomic_primitives.inc").read_text()
        atomic = atomic.replace(marker, "#elif XE_PLATFORM_PS5\n\n" + primitives + "\n" + marker)
        if not target.exists() or target.read_text() != atomic:
            target.write_text(atomic)
    # Narrow test seam: use real instruction emitters without constructing
    # Processor/guest memory. Mirrors the reset done by Emit before each word.
    builder = (source / "src/xenia/cpu/ppc/ppc_hir_builder.h").read_text()
    marker = "  GuestFunction* function() const { return function_; }"
    if builder.count(marker) != 1:
        raise SystemExit("PPC probe seam anchor changed")
    builder = builder.replace(marker, "  void BeginProbeInstruction() { trace_info_.dest_count = 0; }\n\n" + marker)
    target = ROOT / "build/generated/xenia/cpu/ppc/ppc_hir_builder.h"
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_text() != builder:
        target.write_text(builder)
    # Source overlays retain upstream notices. Linux file-offset suffixes do
    # not exist in the FreeBSD-derived PS5 SDK; off_t is already 64-bit there.
    for name in ("filesystem_posix", "mapped_memory_posix", "clock_posix"):
        data = (source / f"src/xenia/base/{name}.cc").read_text()
        replacements = {"fseeko64": "fseeko", "ftello64": "ftello", "off64_t": "off_t",
                        "ftruncate64": "ftruncate", "fstat64": "fstat", "stat64": "stat"}
        for old, new in replacements.items():
            data = data.replace(old, new)
        if name == "clock_posix":
            data = data.replace("CLOCK_MONOTONIC_RAW", "CLOCK_MONOTONIC")
            data = data.replace("1000000000ull / res.tv_nsec", "1000000000ull")
        if name == "filesystem_posix":
            begin = data.index("  char buff[FILENAME_MAX]")
            end = data.index("\n}", begin)
            data = data[:begin] + '#if XE_PLATFORM_PS5\n  return "/app0/eboot.bin";\n#else\n' + data[begin:end] + "\n#endif" + data[end:]
            begin = data.index("  // get preferred data home")
            end = data.index("\n}", begin)
            data = data[:begin] + '#if XE_PLATFORM_PS5\n  return "/download0";\n#else\n' + data[begin:end] + "\n#endif" + data[end:]
        if name == "mapped_memory_posix":
            data = data.replace("if (!data) {", "if (data == MAP_FAILED) {")
            begin = data.index("    size_t map_length = length;")
            end = data.index("\n    void* data =", begin)
            data = data[:begin] + '''    struct stat file_stat;
    if (fstat(file_descriptor, &file_stat) || file_stat.st_size < 0 ||
        offset > size_t(file_stat.st_size) || offset % size_t(getpagesize())) {
      close(file_descriptor); return nullptr;
    }
    const size_t available = size_t(file_stat.st_size) - offset;
    const size_t map_length = length ? length : available;
    if (!map_length || map_length > available) {
      close(file_descriptor); return nullptr;
    }
''' + data[end:]
        target = ROOT / f"build/generated-sources/{name}.cc"
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_text() != data:
            target.write_text(data)
    # The E0000000 alias has a 4 KiB backing offset. The final alias page must
    # fit in the mapping file, including on hosts using 4 KiB allocation pages.
    data = (source / "src/xenia/memory.cc").read_text()
    marker = "0x11FFFFFFF, xe::memory::PageAccess::kReadWrite, false"
    if data.count(marker) != 1:
        raise SystemExit("Guest backing size anchor changed")
    data = data.replace(marker, "0x120001000ull, xe::memory::PageAccess::kReadWrite, false")
    target = ROOT / "build/generated-sources/guest_memory.cc"
    if not target.exists() or target.read_text() != data:
        target.write_text(data)
    # The guest register convention stays unchanged. Adapt only its boundaries
    # to the System V ABI used by Linux and the PS5 native toolchain.
    def overlay(relative, data, header=False):
        target = ROOT / ("build/generated" if header else "build/generated-sources") / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_text() != data:
            target.write_text(data)

    prefix = "xenia/cpu/backend/x64/"
    data = (source / "src" / prefix / "x64_backend.cc").read_text()
    homes = "  mov(qword[rsp + 8 * 3], r8);\n  mov(qword[rsp + 8 * 2], rdx);\n  mov(qword[rsp + 8 * 1], rcx);"
    assert data.count(homes) == 1
    data = data.replace(homes, "#if XE_PLATFORM_WIN32\n" + homes + "\n#endif")
    restore = "  mov(rcx, qword[rsp + 8 * 1]);\n  mov(rdx, qword[rsp + 8 * 2]);\n  mov(r8, qword[rsp + 8 * 3]);"
    assert data.count(restore) == 1
    data = data.replace(restore, "#if XE_PLATFORM_WIN32\n" + restore + "\n#endif")
    body = "  mov(rax, rcx);\n  mov(rsi, rdx);  // context\n  mov(rcx, r8);   // return address"
    assert data.count(body) == 1
    data = data.replace(body, "#if XE_PLATFORM_WIN32\n" + body + "\n#else\n  mov(rax, rdi);  // System V: target, context, return in rdi/rsi/rdx\n  mov(rcx, rdx);\n#endif")
    body = "  mov(rax, rcx);              // function\n  mov(rcx, GetContextReg());  // context"
    assert data.count(body) == 1
    data = data.replace(body, "#if XE_PLATFORM_WIN32\n" + body + "\n#else\n  mov(rax, rcx);\n  mov(rdi, GetContextReg());\n  mov(rsi, rdx);\n  mov(rdx, r8);\n  mov(rcx, r9);\n#endif")
    body = "  mov(rcx, rsi);  // context\n  mov(rdx, rbx);"
    assert data.count(body) == 1
    data = data.replace(body, "#if XE_PLATFORM_WIN32\n" + body + "\n#else\n  mov(rdi, rsi);\n  mov(rsi, rbx);\n#endif")
    data = data.replace("#if XE_PLATFORM_LINUX", "#if !XE_PLATFORM_WIN32")
    for method, action in (("EmitSaveVolatileRegs", "save"), ("EmitLoadVolatileRegs", "load")):
        begin = data.index(f"void X64ThunkEmitter::{method}()")
        end = data.index("\n}", begin)
        extra = "\n#if !XE_PLATFORM_WIN32\n  // System V may clobber every XMM register; guest values must survive.\n"
        for n in range(6, 16):
            slot = f"qword[rsp + offsetof(StackLayout::Thunk, xmm[{n}])]"
            extra += f"  vmovaps({slot}, xmm{n});\n" if action == "save" else f"  vmovaps(xmm{n}, {slot});\n"
        data = data[:end] + extra + "#endif" + data[end:]
    overlay(prefix + "x64_backend.cc", data)
    data = (source / "src" / prefix / "x64_stack_layout.h").read_text()
    assert data.count("    vec128_t xmm[10];") == 1
    data = data.replace("    vec128_t xmm[10];", "#if XE_PLATFORM_WIN32\n    vec128_t xmm[10];\n#else\n    vec128_t xmm[16];\n#endif")
    overlay(prefix + "x64_stack_layout.h", data, True)
    data = (source / "src" / prefix / "x64_emitter.cc").read_text()
    body = "    mov(rax, reinterpret_cast<uint64_t>(ResolveFunction));\n    mov(rcx, GetContextReg());\n    call(rax);"
    assert data.count(body) == 1
    data = data.replace(body, "    CallNativeSafe(reinterpret_cast<void*>(ResolveFunction));")
    # The console loads the title's libc at 0x80000000 and its 16 KiB pages and
    # low address space differ from a desktop's: the indirection table and the
    # generated code move below 2 GiB, near the title's own code.
    load = "    mov(eax, dword[ebx]);"
    assert data.count(load) == 2
    data = data.replace(load, "#if XE_PLATFORM_PS5\n    mov(eax, dword[rbx + X64CodeCache::kIndirectionTableHostDelta]);\n#else\n" + load + "\n#endif")
    guard = "    if (reg.cvt32() != ebx) {\n      mov(ebx, reg.cvt32());\n    }"
    assert data.count(guard) == 1
    data = data.replace(guard, "#if XE_PLATFORM_PS5\n    mov(ebx, reg.cvt32());  // Also clears the upper half used by the 64-bit address.\n#else\n" + guard + "\n#endif")
    overlay(prefix + "x64_emitter.cc", data)
    data = (source / "src" / prefix / "x64_code_cache.h").read_text()
    marker = " protected:\n  // All executable code falls within 0x80000000 to 0x9FFFFFFF"
    assert data.count(marker) == 1
    data = data.replace(marker, "#if XE_PLATFORM_PS5\n  static const intptr_t kIndirectionTableHostDelta = -0x30000000;\n#else\n  static const intptr_t kIndirectionTableHostDelta = 0;\n#endif\n" + marker)
    marker = "  static const uintptr_t kGeneratedCodeExecuteBase = 0xA0000000;"
    assert data.count(marker) == 1
    data = data.replace(marker, "#if XE_PLATFORM_PS5\n  static const uintptr_t kGeneratedCodeExecuteBase = 0x40000000;\n#else\n" + marker + "\n#endif")
    data = data.replace('#include "xenia/base/mutex.h"', '#include "xenia/base/mutex.h"\n#include "xenia/base/platform.h"', 1)
    overlay(prefix + "x64_code_cache.h", data, True)
    data = (source / "src" / prefix / "x64_code_cache.cc").read_text()
    marker = "      reinterpret_cast<void*>(kIndirectionTableBase), kIndirectionTableSize,"
    assert data.count(marker) == 1
    data = data.replace(marker, "      reinterpret_cast<void*>(kIndirectionTableBase + kIndirectionTableHostDelta), kIndirectionTableSize,")
    overlay(prefix + "x64_code_cache.cc", data)
    # A relative Windows font path is not a missing file on the console: stat
    # reports another error and the throwing overload ends the process.
    data = (source / "src/xenia/ui/imgui_drawer.cc").read_text()
    marker = "  if (std::filesystem::exists(jp_font_path)) {"
    assert data.count(marker) == 1
    data = data.replace(marker, "  std::error_code jp_font_error;\n  if (std::filesystem::exists(jp_font_path, jp_font_error)) {")
    overlay("imgui_drawer.cc", data)
    # The game's frame rate, counted where its swap packet is executed.
    data = (source / "src/xenia/gpu/command_processor.cc").read_text()
    marker = '  XELOGI("XE_SWAP");'
    assert data.count(marker) == 1
    data = data.replace(marker, "  xbox360ps5::CountGuestFrame();")
    data = data.replace('#include "xenia/gpu/command_processor.h"', '#include "xenia/gpu/command_processor.h"' + chr(10) + '#include "xbox360ps5/perf.hpp"', 1)
    overlay("command_processor.cc", data)
    # Emulation helpers receive addresses of stashed vectors. Microsoft passes
    # __m128 arguments by reference; make this explicit for System V as well.
    for name in ("x64_sequences.cc", "x64_seq_vector.cc", "x64_tracers.cc", "x64_tracers.h"):
        data = (source / "src" / prefix / name).read_text()
        macro = "#if XE_PLATFORM_WIN32\n#define XE_STASHED_VECTOR(type) type\n#else\n#define XE_STASHED_VECTOR(type) const type&\n#endif\n"
        def vector_params(match):
            head, params = match.group(0).split("(", 1)
            return head + "(" + re.sub(r"(__m128i|__m128d|__m128) (\w+)", r"XE_STASHED_VECTOR(\1) \2", params)
        data = re.sub(r"(?:static )?(?:void|__m128i|__m128d|__m128) (?:Emulate|Trace)\w+\([^)]*\)", vector_params, data)
        overlay(prefix + name, data.replace("namespace xe {", '#include "xenia/base/platform.h"\n' + macro + "\nnamespace xe {", 1), name.endswith(".h"))
    # Real VFS adapters must preserve mixed read/write permissions, file size
    # and bounded error counts. Keep these fixes as generated overlays.
    data = (ROOT / "build/generated-sources/filesystem_posix.cc").read_text()
    start = data.index("  int open_access = 0;", data.index("FileHandle::OpenExisting"))
    end = data.index("  int handle = open(", start)
    data = data[:start] + '''  const bool read = desired_access & (FileAccess::kGenericRead | FileAccess::kGenericExecute | FileAccess::kGenericAll | FileAccess::kFileReadData);
  const bool write = desired_access & (FileAccess::kGenericWrite | FileAccess::kGenericAll | FileAccess::kFileWriteData | FileAccess::kFileAppendData);
  int open_access = read && write ? O_RDWR : write ? O_WRONLY : O_RDONLY;
  if (desired_access & FileAccess::kFileAppendData) open_access |= O_APPEND;
''' + data[end:]
    data = data.replace("    *out_bytes_read = out;", "    *out_bytes_read = out < 0 ? 0 : static_cast<size_t>(out);")
    data = data.replace("    *out_bytes_written = out;", "    *out_bytes_written = out < 0 ? 0 : static_cast<size_t>(out);")
    marker = "    out_info->create_timestamp = convertUnixtimeToWinFiletime(st.st_ctime);"
    assert data.count(marker) == 1
    data = data.replace(marker, "    out_info->total_size = static_cast<uint64_t>(st.st_size);\n" + marker)
    overlay("filesystem_posix.cc", data)
    data = (source / "src/xenia/vfs/devices/host_path_entry.cc").read_text()
    marker = "FileAccess::kFileAppendData)))"
    assert data.count(marker) == 1
    data = data.replace(marker, "FileAccess::kFileAppendData | FileAccess::kGenericWrite | FileAccess::kGenericAll)))")
    marker = "  return MappedMemory::Open(host_path_, mode, offset, length);"
    assert data.count(marker) == 1
    data = data.replace(marker, "  if (is_read_only() && mode != MappedMemory::Mode::kRead) return nullptr;\n" + marker)
    overlay("host_path_entry.cc", data)
    from prepare_threads import generate_threads
    generate_threads(source, ROOT)
    data = (source / "src/xenia/ui/vulkan/vulkan_instance.cc").read_text()
    data = data.replace('#include "xenia/ui/vulkan/vulkan_instance.h"', '#include "xenia/ui/vulkan/vulkan_instance.h"\n#if XE_PLATFORM_PS5\n#include "xbox360ps5/radv_dispatch.hpp"\n#endif', 1)
    marker = "#else\n#error No Vulkan loader library loading provided for the target platform."
    assert data.count(marker) == 1
    data = data.replace(marker, '#elif XE_PLATFORM_PS5\n#define XE_VULKAN_LOAD_LOADER_FUNCTION(name) functions_loaded &= (ifn.name = PFN_##name(xbox360ps5::RadvLoaderProc(#name))) != nullptr;\n#else\n#error No Vulkan loader library loading provided for the target platform.')
    overlay("vulkan_instance.cc", data)
    data = (source / "src/xenia/kernel/xam/xam_net.cc").read_text()
    data = data.replace("#elif XE_PLATFORM_LINUX", "#elif XE_PLATFORM_LINUX || XE_PLATFORM_PS5")
    overlay("xam_net.cc", data)
    data = (source / "src/xenia/kernel/xboxkrnl/xboxkrnl_io_info.cc").read_text()
    marker = ('      // TODO(benvanik): return sector this file\'s on.\n'
              '      XELOGE("NtQueryInformationFile(XFileSectorInformation) unimplemented");\n'
              '      status = X_STATUS_INVALID_PARAMETER;\n'
              '      out_length = 0;\n')
    assert data.count(marker) == 1
    data = data.replace(marker,
        '      // Games use it to tell files apart (Xenia Canary does the same).\n'
        '      auto info = info_ptr.as<uint32_t*>();\n'
        '      const size_t path_hash = xe::memory::hash_combine(82589933LL, file->path());\n'
        '      *info = static_cast<uint32_t>(path_hash ^ (path_hash >> 32));\n'
        '      out_length = sizeof(uint32_t);\n')
    overlay("xboxkrnl_io_info.cc", data)
    data = (source / "src/xenia/kernel/xam/xam_info.cc").read_text()
    marker = "      if (xe::utf8::find_name_from_guest_path(path) == path) {"
    assert data.count(marker) == 1
    # "DBZ3\\yae3_xenon.xex" (a folder and a file, no device) is relative too.
    data = data.replace(marker, "      if (path.find(':') == std::string::npos && path[0] != '\\\\') {")
    marker = "  // This function does not return.\n  kernel_state()->TerminateTitle();\n}\nDECLARE_XAM_EXPORT1(XamLoaderLaunchTitle"
    assert data.count(marker) == 1
    data = data.replace(marker, "  xbox360ps5::RelaunchInto(raw_name_ptr ? loader_data.launch_path : std::string(), loader_data.launch_data);\n" + marker)
    marker = "void XamLoaderTerminateTitle_entry() {\n"
    assert data.count(marker) == 1
    data = data.replace(marker, marker + "  xbox360ps5::RelaunchInto(std::string(), {});\n")
    data = data.replace('#include "xenia/kernel/xam/xam_module.h"', '#include "xenia/kernel/xam/xam_module.h"\n#include "xbox360ps5/title_switch.hpp"', 1)
    assert "title_switch.hpp" in data
    overlay("xam_info.cc", data)
    data = (source / "src/xenia/kernel/xsocket.cc").read_text()
    data = data.replace("#elif XE_PLATFORM_LINUX", "#elif XE_PLATFORM_LINUX || XE_PLATFORM_PS5")
    data = data.replace("#include <unistd.h>", "#include <unistd.h>\n#if XE_PLATFORM_PS5\n#undef IPPROTO_UDP\n#endif", 1)
    overlay("xsocket.cc", data)
    overlay("endian.h", '''#pragma once
#if defined(__FreeBSD__)
#include <sys/endian.h>
#define __BYTE_ORDER _BYTE_ORDER
#define __BIG_ENDIAN _BIG_ENDIAN
#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#else
#include_next <endian.h>
#endif
''', True)
    overlay("build/version.h", '#pragma once\n#define XE_BUILD_COMMIT "' + actual + '"\n', True)
    data = (source / "src/xenia/emulator.cc").read_text()
    marker = "  if (!memory_->Initialize()) {\n    return false;\n  }"
    assert data.count(marker) == 1
    data = data.replace(marker, "  if (!memory_->Initialize()) {\n    return X_STATUS_UNSUCCESSFUL;\n  }")
    # On the console, suspending the other threads from a fault handler ends the
    # process before the guest crash dump is written (a thread suspended inside
    # a condition wait cannot wait again in its signal handler). Report the
    # host fault and the guest registers, then let the fault take its course.
    marker = "  // Within range. Pause the emulator and eat the exception.\n  Pause();"
    assert data.count(marker) == 1
    data = data.replace(marker, "#if !XE_PLATFORM_PS5\n" + marker + "\n#endif")
    marker = '  auto guest_function = code_cache->LookupFunction(ex->pc());\n  assert_not_null(guest_function);'
    assert data.count(marker) == 1
    data = data.replace(marker, '  XELOGE("Host PC: {:016X}, code {}, fault address {:016X}, operation {}",\n'
        '         ex->pc(), int(ex->code()),\n'
        '         ex->code() == Exception::Code::kAccessViolation ? ex->fault_address() : 0,\n'
        '         ex->code() == Exception::Code::kAccessViolation ? int(ex->access_violation_operation()) : -1);\n'
        '  auto guest_function = code_cache->LookupFunction(ex->pc());\n'
        '#if XE_PLATFORM_PS5\n  if (!guest_function) {\n    XELOGE("Host PC is not inside a translated guest function");\n    xbox360ps5::DumpGuestCrash(memory(), current_thread->thread_state()->context(), current_thread->thread_id());\n    return false;\n  }\n#endif')
    marker = '  XELOGE("Registers:");'
    assert data.count(marker) == 1
    data = data.replace(marker, '  XELOGE("LR: 0x{:08X} CTR: 0x{:08X}", uint32_t(context->lr), uint32_t(context->ctr));\n' + marker)
    marker = '  for (int i = 0; i < 32; i++) {\n    XELOGE(" f{:<3}'
    assert data.count(marker) == 1
    data = data.replace(marker, "#if XE_PLATFORM_PS5\n  return false;\n#endif\n" + marker)
    data = data.replace('#include "xenia/emulator.h"', '#include "xenia/emulator.h"\n#if XE_PLATFORM_PS5\n#include "xbox360ps5/guest_crash_dump.hpp"\n#endif', 1)
    # Game patches (Canary's .patch.toml files) go in after the executable is
    # loaded and before its first thread runs.
    marker = "  auto main_thread = kernel_state_->LaunchModule(module);"
    assert data.count(marker) == 1
    data = data.replace(marker, "  xbox360ps5::ApplyGamePatches(memory(), module.get(), title_id_.value());\n" + marker)
    data = data.replace('#include "xenia/emulator.h"', '#include "xenia/emulator.h"\n#include "xbox360ps5/game_patches.hpp"', 1)
    # After a title switch (a restart, see title_switch.hpp) the game is mounted
    # as before and the executable it asked for runs instead of its default.
    marker = ("X_STATUS Emulator::CompleteLaunch(const std::filesystem::path& path,\n"
              "                                  const std::string_view module_path) {\n")
    assert data.count(marker) == 1
    data = data.replace(marker, marker.replace("module_path", "requested_module_path") +
        "  const std::string module_override = xbox360ps5::TakeModuleOverride();\n"
        "  const std::string_view module_path =\n"
        "      module_override.empty() ? requested_module_path : std::string_view(module_override);\n"
        "  // As on the console, game: and d: become the folder of the executable started\n"
        "  // (Budokai 3 in the HD Collection reads D:\\\\us\\\\data_cmn.afs from DBZ3).\n"
        "  if (const size_t folder_end = module_override.rfind('\\\\'); folder_end != std::string::npos) {\n"
        "    for (const char* link : {\"game:\", \"d:\"}) {\n"
        "      file_system_->UnregisterSymbolicLink(link);\n"
        "      file_system_->RegisterSymbolicLink(link, module_override.substr(0, folder_end));\n"
        "    }\n"
        "  }\n")
    data = data.replace('#include "xenia/emulator.h"', '#include "xenia/emulator.h"\n#include "xbox360ps5/title_switch.hpp"', 1)
    # Per-game settings shipped with the title (assets/game-configs) and the
    # user's own, in Xenia's game config format.
    marker = "    config::LoadGameConfig(title_id);\n"
    assert data.count(marker) == 1
    data = data.replace(marker, "    xbox360ps5::LoadGameConfigs(title_id);\n")
    overlay("emulator.cc", data)
    data = (source / "src/xenia/gpu/vulkan/vulkan_graphics_system.cc").read_text()
    marker = "  provider_ = xe::ui::vulkan::VulkanProvider::Create(true, with_presentation);"
    assert data.count(marker) == 1
    data = data.replace(marker, marker + "\n  if (!provider_) return X_STATUS_UNSUCCESSFUL;")
    overlay("vulkan_graphics_system.cc", data)
    # Render-to-texture results read back into guest memory, as D3D12's
    # d3d12_readback_resolve does; the Vulkan backend only had a TODO. Games
    # that read a resolved picture on the CPU (exposure, saves) need it.
    data = (source / "src/xenia/gpu/vulkan/vulkan_command_processor.cc").read_text()
    marker = "  // TODO(Triang3l): CPU readback.\n"
    assert data.count(marker) == 1
    data = data.replace(marker, """  if ((cvars::readback_resolve || xbox360ps5_draw_log == 2) &&
      !texture_cache_->IsDrawResolutionScaled() && written_length) {
    ReadbackToGuest(written_address, written_length);
    // While a frame is logged: each resolved picture to a file, to see
    // which pass of the frame goes wrong.
    if (const char* folder = std::getenv("XBOX360PS5_RESOLVE_DIR");
        folder && xbox360ps5_draw_log == 2) {
      static int resolve_index = 0;
      const std::string name = fmt::format("{}/resolve-{:02d}-{:08X}.bin", folder,
                                           resolve_index++, written_address);
      if (std::FILE* file = std::fopen(name.c_str(), "wb")) {
        std::fwrite(memory_->TranslatePhysical(written_address), 1, written_length, file);
        std::fclose(file);
      }
    }
  }
""")
    marker = "void VulkanCommandProcessor::ShutdownContext() {\n"
    assert data.count(marker) == 1
    data = data.replace(marker, marker + "  ReleaseReadbackBuffer(GetVulkanDevice());\n")
    marker = "bool VulkanCommandProcessor::IssueCopy() {\n"
    assert data.count(marker) == 1
    data = data.replace(marker, """void VulkanCommandProcessor::ReadbackToGuest(uint32_t address, uint32_t length) {
  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  if (length > readback_size) {
    ReleaseReadbackBuffer(vulkan_device);
    const uint32_t size = xe::align(length, uint32_t(4) << 20);
    if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(
            vulkan_device, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            ui::vulkan::util::MemoryPurpose::kReadback, readback_buffer,
            readback_memory)) {
      XELOGE("Failed to create a {} KB resolve readback buffer", size >> 10);
      return;
    }
    readback_size = size;
  }
  shared_memory_->Use(VulkanSharedMemory::Usage::kRead);
  SubmitBarriers(true);
  VkBufferCopy* region = deferred_command_buffer_.CmdCopyBufferEmplace(
      shared_memory_->buffer(), readback_buffer, 1);
  region->srcOffset = address;
  region->dstOffset = 0;
  region->size = length;
  PushBufferMemoryBarrier(readback_buffer, 0, VK_WHOLE_SIZE,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_HOST_BIT,
                          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
  SubmitBarriers(true);
  if (!AwaitAllQueueOperationsCompletion()) return;
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  void* mapping;
  if (dfn.vkMapMemory(vulkan_device->device(), readback_memory, 0, VK_WHOLE_SIZE,
                      0, &mapping) != VK_SUCCESS) {
    return;
  }
  std::memcpy(memory_->TranslatePhysical(address), mapping, length);
  dfn.vkUnmapMemory(vulkan_device->device(), readback_memory);
}

""" + marker)
    marker = '#include "xenia/gpu/vulkan/vulkan_command_processor.h"\n'
    assert data.count(marker) == 1
    data = data.replace(marker, marker + """#include <cstring>
#include "xenia/ui/vulkan/vulkan_util.h"
#include <atomic>
// See LogDrawForDebug.
std::atomic<int> xbox360ps5_draw_log{0};
namespace {
VkBuffer readback_buffer = VK_NULL_HANDLE;
VkDeviceMemory readback_memory = VK_NULL_HANDLE;
uint32_t readback_size = 0;
void ReleaseReadbackBuffer(const xe::ui::vulkan::VulkanDevice* vulkan_device) {
  if (!readback_buffer) return;
  const xe::ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  dfn.vkDestroyBuffer(vulkan_device->device(), readback_buffer, nullptr);
  dfn.vkFreeMemory(vulkan_device->device(), readback_memory, nullptr);
  readback_buffer = VK_NULL_HANDLE;
  readback_memory = VK_NULL_HANDLE;
  readback_size = 0;
}
}  // namespace
DEFINE_bool(readback_resolve, false,
            "Read render-to-texture results back into guest memory. Needed by "
            "games that read a rendered picture on the CPU; slow, because it "
            "waits for the GPU at every resolve.",
            "GPU");
""")
    # The texture fetch result exponent bias (exp_adjust) is in bits 13:18 of
    # fetch constant word 3, as xenos.h and the DXBC translator have it; the
    # SPIR-V translator took word 4 (the LOD bias), scaling every texture with a
    # LOD bias by a wrong power of two. Dragon Ball Z Burst Limit's depth
    # texture came out divided by 65536, and the whole post-processed picture
    # black.
    fetch = (source / "src/xenia/gpu/spirv_shader_translator_fetch.cc").read_text()
    marker = ("        spv::Id fetch_constant_word_4_signed = builder_->createUnaryOp(\n"
              "            spv::OpBitcast, type_int_, fetch_constant_word_4);\n")
    assert fetch.count(marker) == 1
    fetch = fetch.replace(marker, marker + """        id_vector_temp_.clear();
        id_vector_temp_.push_back(const_int_0_);
        id_vector_temp_.push_back(builder_->makeIntConstant(
            int((fetch_constant_word_0_index + 3) >> 2)));
        id_vector_temp_.push_back(builder_->makeIntConstant(
            int((fetch_constant_word_0_index + 3) & 3)));
        spv::Id fetch_constant_word_3_signed = builder_->createUnaryOp(
            spv::OpBitcast, type_int_,
            builder_->createLoad(
                builder_->createAccessChain(spv::StorageClassUniform,
                                            uniform_fetch_constants_,
                                            id_vector_temp_),
                spv::NoPrecision));
""")
    marker = ("        // Apply the exponent bias from the bits 13:18 of the fetch constant\n"
              "        // word 4.\n"
              "        spv::Id result_exponent_bias = builder_->createBinBuiltinCall(\n"
              "            type_float_, ext_inst_glsl_std_450_, GLSLstd450Ldexp,\n"
              "            const_float_1_,\n"
              "            builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
              "                                  fetch_constant_word_4_signed,\n")
    assert fetch.count(marker) == 1
    fetch = fetch.replace(marker, marker.replace("word 4.", "word 3.").replace("fetch_constant_word_4_signed,", "fetch_constant_word_3_signed,"))
    overlay("spirv_shader_translator_fetch.cc", fetch)
    # Debugging aid: every draw of one frame in the log (shaders, render
    # targets, textures, a few constants), when xbox360ps5_draw_log is set to 1
    # (by the PC test runner); logging starts at the next swap and stops at the one after.
    marker = "  if (pixel_shader && pixel_shader->memexport_eM_written() != 0 &&\n      device_properties.fragmentStoresAndAtomics) {\n"
    assert data.count(marker) == 1
    data = data.replace(marker, "  if (xbox360ps5_draw_log == 2) LogDrawForDebug(regs, vertex_shader, pixel_shader, prim_type, index_count, is_rasterization_done);\n" + marker)
    marker = "void VulkanCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,\n"
    assert data.count(marker) == 1
    data = data.replace(marker, """void LogDrawForDebug(const RegisterFile& regs, const Shader* vs, const Shader* ps,
                     xenos::PrimitiveType prim_type, uint32_t index_count, bool raster) {
  XELOGW("DRAW vs {:016X} ps {:016X} prim {} count {} raster {} surface {:08X} color {:08X} {:08X} {:08X} {:08X} "
         "depth {:08X} mask {:08X} depthctl {:08X} blend {:08X} mode {:08X} sc {:08X} scissor {:08X}",
         vs ? vs->ucode_data_hash() : 0, ps ? ps->ucode_data_hash() : 0, uint32_t(prim_type), index_count, raster,
         regs[XE_GPU_REG_RB_SURFACE_INFO], regs[XE_GPU_REG_RB_COLOR_INFO], regs[XE_GPU_REG_RB_COLOR1_INFO],
         regs[XE_GPU_REG_RB_COLOR2_INFO], regs[XE_GPU_REG_RB_COLOR3_INFO], regs[XE_GPU_REG_RB_DEPTH_INFO],
         regs[XE_GPU_REG_RB_COLOR_MASK], regs[XE_GPU_REG_RB_DEPTHCONTROL], regs[XE_GPU_REG_RB_BLENDCONTROL0],
         regs[XE_GPU_REG_RB_MODECONTROL], regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL], regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR]);
  for (const Shader* shader : {vs, ps}) {
    if (!shader) continue;
    for (const auto& binding : shader->texture_bindings()) {
      const uint32_t* f = &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + binding.fetch_constant * 6];
      XELOGW("  {} tex {} fetch {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}", shader == vs ? "vs" : "ps",
             binding.fetch_constant, f[0], f[1], f[2], f[3], f[4], f[5]);
    }
    const auto& map = shader->constant_register_map();
    const uint32_t base = shader == vs ? XE_GPU_REG_SHADER_CONSTANT_000_X : XE_GPU_REG_SHADER_CONSTANT_256_X;
    std::string constants;
    for (uint32_t c = 0, shown = 0; c < 256 && shown < 12; ++c) {
      if (!(map.float_bitmap[c >> 6] >> (c & 63) & 1)) continue;
      const float* v = reinterpret_cast<const float*>(&regs[base + c * 4]);
      constants += fmt::format(" c{}=({:g},{:g},{:g},{:g})", c, v[0], v[1], v[2], v[3]);
      ++shown;
    }
    if (!constants.empty()) XELOGW("  {} consts{}", shader == vs ? "vs" : "ps", constants);
  }
}

""" + marker)
    marker = "  ui::Presenter* presenter = graphics_system_->presenter();\n  if (!presenter) {\n    return;\n  }\n"
    assert data.count(marker) == 1
    data = data.replace(marker, "  if (xbox360ps5_draw_log == 1 || xbox360ps5_draw_log == 2) {\n"
                                "    XELOGW(\"DRAW LOG swap {:08X} {}x{}\", frontbuffer_ptr, frontbuffer_width, frontbuffer_height);\n"
                                "    ++xbox360ps5_draw_log;\n  }\n" + marker)
    overlay("vulkan_command_processor.cc", data)
    data = (source / "src/xenia/gpu/vulkan/vulkan_command_processor.h").read_text()
    marker = "  bool IssueCopy() override;\n"
    assert data.count(marker) == 1
    data = data.replace(marker, marker + "  void ReadbackToGuest(uint32_t address, uint32_t length);\n")
    overlay("xenia/gpu/vulkan/vulkan_command_processor.h", data, True)
    data = (source / "third_party/renderdoc/renderdoc_app.h").read_text()
    marker = "#elif defined(__linux__)"
    assert data.count(marker) == 1
    data = data.replace(marker, "#elif defined(__linux__) || defined(__FreeBSD__)")
    overlay("third_party/renderdoc/renderdoc_app.h", data, True)
    from prepare_display import generate_display
    generate_display(source, ROOT)
    data = (source / "src/xenia/ui/vulkan/spirv_tools_context.h").read_text()
    data = data.replace("#if XE_PLATFORM_LINUX\n  void* library_", "#if XE_PLATFORM_LINUX || XE_PLATFORM_PS5\n  void* library_")
    marker = "#else\n#error No SPIRV-Tools LoadLibraryFunction provided for the target platform."
    assert data.count(marker) == 1
    data = data.replace(marker, "#elif XE_PLATFORM_PS5\n    function = nullptr;  // Optional validator is not linked on this target.\n#else\n#error No SPIRV-Tools LoadLibraryFunction provided for the target platform.")
    overlay("xenia/ui/vulkan/spirv_tools_context.h", data, True)
    data = (source / "src/xenia/ui/vulkan/spirv_tools_context.cc").read_text()
    marker = "#else\n#error No SPIRV-Tools library loading provided for the target platform."
    assert data.count(marker) == 1
    data = data.replace(marker, '#elif XE_PLATFORM_PS5\n  XELOGE("SPIRV-Tools: optional validator not linked in PS5 title");\n  return false;\n#else\n#error No SPIRV-Tools library loading provided for the target platform.')
    overlay("spirv_tools_context.cc", data)
    data = (source / "src/xenia/base/profiling.cc").read_text()
    # Match upstream's release-mode profiling.h: no optional MicroProfile
    # implementation is needed when the profiling interface is disabled.
    data = data.replace("#define MICROPROFILE_ENABLED 1", "#ifndef NDEBUG\n#define MICROPROFILE_ENABLED 1", 1)
    data = data.replace('#include "third_party/microprofile/microprofile.h"', '#include "third_party/microprofile/microprofile.h"\n#endif', 1)
    overlay("profiling.cc", data)
    data = (source / "src/xenia/apu/xma_decoder.cc").read_text()
    marker = "    if (context.Setup(i, memory(), guest_ptr)) {\n      assert_always();\n    }"
    assert data.count(marker) == 1
    data = data.replace(marker, "    if (context.Setup(i, memory(), guest_ptr)) {\n      return X_STATUS_UNSUCCESSFUL;\n    }")
    marker = "    switch (r) {\n      default: {\n        const auto register_info = register_file_.GetRegisterInfo(r);"
    assert data.count(marker) == 1
    data = data.replace(marker, "    switch (r) {\n      case 0x601:\n        break;  // Written around each context lock.\n      default: {\n        const auto register_info = register_file_.GetRegisterInfo(r);")
    overlay("xma_decoder.cc", data)
    data = (source / "src/xenia/apu/xma_context.cc").read_text()
    marker = "XmaContext::~XmaContext() {"
    assert data.count(marker) == 1
    data = data.replace(marker, marker + "\n  av_packet_free(&av_packet_);")
    overlay("xma_context.cc", data)
    data = (source / "src/xenia/cpu/thread.h").read_text()
    marker = "thread_local static Thread* current_thread_;"
    assert data.count(marker) == 1
    # Its upstream definition is initialized with nullptr. Publish that fact
    # across translation units so Clang doesn't emit an absent weak TLS-init
    # function that the native module converter would mistake for an import.
    data = data.replace(marker, "constinit thread_local static Thread* current_thread_;")
    overlay("xenia/cpu/thread.h", data, True)
    print(f"Pinned Xenia {actual}; PS5 overlay generated")


if __name__ == "__main__":
    main()
