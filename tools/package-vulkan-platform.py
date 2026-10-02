"""Package M6 platform-boundary evidence, not an installable emulator."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from artifact_contracts import check_archive

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "build/vulkan-host"
TARGET = ROOT / "build/vulkan-ps5"
tests = ET.parse(HOST / "vulkan-tests.xml").getroot()
if int(tests.get("tests", "0")) != 9 or any(int(tests.get(key, "0")) for key in ("failures", "errors", "skipped", "disabled")):
    raise SystemExit("Nine passing host suites required")
outputs = {case.get("name"): case.findtext("system-out", "") for case in tests.findall("testcase")}
markers = {
    "xenon_opcode_decoder": "Decoder cases: 9; failures: 0",
    "xenon_hir_semantics": "HIR CASES 20 FAILURES 0",
    "ps5_atomic_semantics": "ATOMIC CASES 13 FAILURES 0",
    "xenon_optimizer_semantics": "COMPILER CASES 24 FAILURES 0",
    "x64_codegen_decode": "CODEGEN CASES 8 FAILURES 0",
    "ppc_instruction_translation": "PPC TRANSLATION CASES 24 FAILURES 0",
    "actual_ppc_x64_execution": "JIT RUNTIME CASES 50 FAILURES 0",
    "guest_memory_contract": "MEMORY CASES 39 FAILURES 0",
    "vulkan_platform_contract": "VULKAN PLATFORM CASES 24 FAILURES 0 (mock dispatch, no hardware)",
}
for name, marker in markers.items():
    if marker not in outputs.get(name, ""):
        raise SystemExit(f"Missing evidence: {name}")
log = (ROOT / "build/vulkan-host.log").read_text()
if "VULKAN TRANSFER WORDS 1024 FAILURES 0 (host driver, not PS5)" not in log or "Ran 12 tests" not in log or "\nOK\n" not in log:
    raise SystemExit("Real host-driver readback and Python test evidence required")
source_paths = [
    "CMakeLists.txt", "cmake/VulkanPlatform.cmake", "build.ps1", "deps.json",
    "include/xbox360ps5/vulkan_platform.hpp", "platform/ps5/vulkan_platform.cpp",
    "src/vulkan_platform_contract.cpp", "src/vulkan_transfer_probe.cpp",
    "tools/build-vulkan-platform.sh", "tools/package-vulkan-platform.py",
    "tools/verify.py", "tools/artifact_contracts.py", "tooling/docker/Dockerfile.vulkan",
    "docs/PS5_REFERENCES.md",
]
for artifact, inputs in {
    TARGET / "libxbox360ps5_vulkan_platform.a": source_paths[4:6],
    TARGET / "vulkan-platform-contract": source_paths[4:7],
    HOST / "vulkan-platform-contract": source_paths[4:7],
    HOST / "vulkan-transfer-probe": source_paths[4:6] + [source_paths[7]],
}.items():
    if any((ROOT / relative).stat().st_mtime > artifact.stat().st_mtime for relative in inputs):
        raise SystemExit(f"Rebuild stale artifact: {artifact}")
subprocess.check_call([sys.executable, str(ROOT / "tools/verify.py"), "vulkan-platform-contract", "--build-dir", str(TARGET)])
archive = check_archive(TARGET / "libxbox360ps5_vulkan_platform.a")
report = {
    "stage": "M6 Vulkan platform boundary", "host_suites": 9,
    "host_internal_checks": 211, "vulkan_contract_checks": 24,
    "host_driver": "Lavapipe software Vulkan", "host_transfer_words_checked": 1024,
    "python_tests": 12, "hardware_tested": False, "game_executed": False,
    "radv_linked": False, "xenos_integrated": False, "installable_title": False,
    "archive": archive,
    "elf": json.loads((TARGET / "vulkan-receipt.json").read_text()),
    "sources": {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in source_paths},
    "vulkan_headers_revision": "31aa7f634b052d87ede4664053e85f3f4d1d50d3",
}
evidence = ROOT / "docs/evidence"
evidence.mkdir(exist_ok=True)
(evidence / "M6_VULKAN_PLATFORM.json").write_text(json.dumps(report, indent=2) + "\n")
(evidence / "M6_HOST.xml").write_bytes((HOST / "vulkan-tests.xml").read_bytes())
(evidence / "M6_HOST_TRANSFER.txt").write_text("\n".join(line for line in log.splitlines() if line.startswith(("HOST DEVICE", "VULKAN TRANSFER", "Ran 12 tests", "OK"))) + "\n")
files = {"target/libxbox360ps5_vulkan_platform.a": TARGET / "libxbox360ps5_vulkan_platform.a",
         "target/vulkan-platform-contract": TARGET / "vulkan-platform-contract",
         "target/vulkan-receipt.json": TARGET / "vulkan-receipt.json",
         "evidence/M6_VULKAN_PLATFORM.json": evidence / "M6_VULKAN_PLATFORM.json",
         "evidence/M6_HOST.xml": evidence / "M6_HOST.xml",
         "evidence/M6_HOST_TRANSFER.txt": evidence / "M6_HOST_TRANSFER.txt"}
files.update({"source/" + path: ROOT / path for path in source_paths})
files["licenses/XENIA-BSD.txt"] = ROOT / "licenses/XENIA-BSD.txt"
header_root = ROOT / ".deps/xenia/third_party/Vulkan-Headers"
for path in (header_root / "LICENSE.md", *sorted((header_root / "LICENSES").glob("*"))):
    if path.is_file(): files["licenses/Vulkan-Headers/" + path.name] = path
destination = ROOT / "dist/Xbox360PS5-M6-vulkan-platform-development.zip"
destination.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as package:
    for name, path in sorted(files.items()): package.write(path, name)
print(json.dumps({"package": str(destination), "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(), "bytes": destination.stat().st_size,
                  "host_checks": 211, "host_transfer_words": 1024, "hardware_tested": False}))
