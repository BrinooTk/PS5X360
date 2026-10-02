"""Bundle validated M5 development artifacts. No title, game or game launch."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from artifact_contracts import check_archive

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/runtime-ps5"
tests = ET.parse(ROOT / "build/runtime-host/runtime-tests.xml").getroot()
if int(tests.get("tests", "0")) != 8 or any(int(tests.get(key, "0")) for key in ("failures", "errors", "skipped", "disabled")):
    raise SystemExit("Eight passing host suites required")
outputs = {test.get("name"): test.findtext("system-out", "") for test in tests.findall("testcase")}
for suite, marker in {
    "xenon_opcode_decoder": "Decoder cases: 9; failures: 0",
    "xenon_hir_semantics": "HIR CASES 20 FAILURES 0",
    "ps5_atomic_semantics": "ATOMIC CASES 13 FAILURES 0",
    "xenon_optimizer_semantics": "COMPILER CASES 24 FAILURES 0",
    "x64_codegen_decode": "CODEGEN CASES 8 FAILURES 0",
    "ppc_instruction_translation": "PPC TRANSLATION CASES 24 FAILURES 0",
}.items():
    if marker not in outputs.get(suite, ""):
        raise SystemExit(f"Missing baseline evidence: {suite}")
if "JIT RUNTIME CASES 50 FAILURES 0" not in outputs.get("actual_ppc_x64_execution", ""):
    raise SystemExit("Untruncated evidence of 50 successful actual JIT checks required")
if "MEMORY CASES 39 FAILURES 0" not in outputs.get("guest_memory_contract", ""):
    raise SystemExit("Evidence of 39 successful memory checks required")
for name in ("xenia-memory-contract", "xenia-runtime-link"):
    subprocess.check_call([sys.executable, str(ROOT / "tools/verify.py"), name, "--build-dir", str(BUILD)])

archives = {}
for name in ("cpu_runtime", "platform_memory", "base_runtime", "x64_backend", "cpu_compiler", "ppc_frontend", "hir_values", "cpu_config", "ppc_decoder", "capstone"):
    path = BUILD / f"libxenia_{name}.a"
    archives[path.name] = check_archive(path)

report = {"stage": "M5 actual CPU runtime development gate", "host_test_suites": 8,
          "host_internal_cases": 187, "host_jit_cases": 50, "host_memory_cases": 39,
          "hardware_tested": False, "game_executed": False, "kernel_guest_integrated": False,
          "vulkan_integrated": False, "installable_title": False, "archives": archives,
          "dependencies": json.loads((ROOT / "deps.json").read_text())}
sources = [ROOT / "CMakeLists.txt", ROOT / "cmake/Runtime.cmake", ROOT / "tools/prepare.py",
           ROOT / "src/runtime_link_gate.cpp", ROOT / "src/memory_contract_test.cpp"]
sources += [ROOT / "platform/ps5" / (name + ".cpp") for name in
            ("memory", "logging", "thread_primitives", "exception_handler")]
report["source_sha256"] = {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
(BUILD / "development-receipt.json").write_text(json.dumps(report, indent=2) + "\n")
items = {"RUNTIME_INTEGRATION.md": ROOT / "docs/RUNTIME_INTEGRATION.md",
         "LICENSE": ROOT / "LICENSE", "host-runtime-tests.xml": ROOT / "build/runtime-host/runtime-tests.xml"}
for name in archives:
    items[name] = BUILD / name
for name in ("xenia-memory-contract", "xenia-runtime-link"):
    items[name + ".elf"] = BUILD / name
for name in ("memory-receipt", "runtime-receipt", "development-receipt"):
    items[name + ".json"] = BUILD / (name + ".json")
for path in (ROOT / "licenses").glob("*"):
    if path.is_file(): items["licenses/" + path.name] = path
for dep, filename in (("xbyak", "COPYRIGHT"), ("capstone", "LICENSE.TXT"), ("cpptoml", "LICENSE"),
                      ("cxxopts", "LICENSE"), ("date", "LICENSE.txt"), ("llvm", "LICENSE.txt"),
                      ("utfcpp", "LICENSE"), ("half", "LICENSE.txt")):
    path = ROOT / ".deps/xenia/third_party" / dep / filename
    if not path.is_file(): raise SystemExit(f"Missing dependency license: {path}")
    items[f"licenses/{dep.upper()}.txt"] = path
target = ROOT / "dist/Xbox360PS5-M5-runtime-development.zip"
target.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as package:
    for name, path in items.items(): package.write(path, name)
with zipfile.ZipFile(target) as package:
    if package.testzip(): raise SystemExit("Corrupt ZIP")
print(json.dumps({"package": str(target), "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
                  "bytes": target.stat().st_size, "game_ready": False}))
