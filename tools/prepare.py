"""Fetch pinned inputs and generate a PS5 platform overlay without editing Xenia."""
import argparse
import json
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
    dep = json.loads((ROOT / "deps.json").read_text())["xenia"]
    source = ROOT / ".deps/xenia"
    if args.fetch:
        if not source.exists():
            subprocess.check_call(["git", "clone", "--no-checkout", dep["url"], str(source)])
        if run("git", "-C", str(source), "status", "--porcelain"):
            raise SystemExit("Xenia has local changes; refusing checkout")
        if subprocess.run(["git", "-C", str(source), "cat-file", "-e", dep["revision"]],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            subprocess.check_call(["git", "-C", str(source), "fetch", "origin", dep["revision"]])
        subprocess.check_call(["git", "-C", str(source), "checkout", "--detach", dep["revision"]])
        subprocess.check_call(["git", "-C", str(source), "submodule", "update", "--init",
                               "--depth", "1", *dep["submodules"]])
    actual = run("git", "-C", str(source), "rev-parse", "HEAD")
    if actual != dep["revision"]:
        raise SystemExit(f"Wrong Xenia revision: {actual}")
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
    atomic = (source / "src/xenia/base/atomic.h").read_text()
    marker = "#elif XE_PLATFORM_LINUX || XE_PLATFORM_MAC"
    if atomic.count(marker) != 1:
        raise SystemExit("Atomic overlay anchor changed")
    primitives = (ROOT / "platform/ps5/atomic_primitives.inc").read_text()
    atomic = atomic.replace(marker, "#elif XE_PLATFORM_PS5\n\n" + primitives + "\n" + marker)
    target = ROOT / "build/generated/xenia/base/atomic.h"
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
    overlay(prefix + "x64_emitter.cc", data)
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
    overlay("emulator.cc", data)
    data = (source / "src/xenia/gpu/vulkan/vulkan_graphics_system.cc").read_text()
    marker = "  provider_ = xe::ui::vulkan::VulkanProvider::Create(true, with_presentation);"
    assert data.count(marker) == 1
    data = data.replace(marker, marker + "\n  if (!provider_) return X_STATUS_UNSUCCESSFUL;")
    overlay("vulkan_graphics_system.cc", data)
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
