"""Validate and bundle the CPU compilation gate, not an installable emulator."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from artifact_contracts import check_archive

root = Path(__file__).resolve().parents[1]
probes = ("platform", "hir", "atomic", "compiler", "codegen", "ppc-translation")
for probe in probes:
    subprocess.check_call([sys.executable, str(root / "tools/verify.py"), f"xenia-{probe}-smoke"])

tests = ET.parse(root / "build/host/cpu-tests.xml").getroot()
if int(tests.get("tests", "0")) != 6 or any(int(tests.get(name, "0")) for name in ("failures", "errors", "skipped", "disabled")):
    raise SystemExit("Six successful host suites required")

archives = {}
for name in ("ppc_decoder", "hir_values", "ppc_frontend", "cpu_compiler", "x64_backend", "capstone", "cpu_config"):
    path = root / "build/ps5" / f"libxenia_{name}.a"
    archives[path.name] = check_archive(path)
dependencies = json.loads((root / "deps.json").read_text())
submodules = {}
for name in dependencies["xenia"]["submodules"]:
    submodules[name] = subprocess.check_output(
        ["git", "-C", str(root / ".deps/xenia" / name), "rev-parse", "HEAD"], text=True).strip()
report = {"stage": "M4 synthetic PPC translation gate", "archives": archives,
          "dependencies": dependencies, "submodules": submodules,
          "host_test_suites": 6, "hardware_tested": False,
          "frontend_fully_linked": False, "guest_execution": False, "jit_execution": False,
          "note": "Archives retain runtime dependencies; compiling is not full CPU integration."}
(root / "build/ps5/cpu-receipt.json").write_text(json.dumps(report, indent=2) + "\n")

items = {"CPU_TRANSLATION.md": root / "docs/CPU_TRANSLATION.md",
         "LICENSE": root / "LICENSE", "host-cpu-tests.xml": root / "build/host/cpu-tests.xml"}
for name in archives:
    items[name] = root / "build/ps5" / name
for probe in probes:
    items[f"xenia-{probe}-smoke.elf"] = root / "build/ps5" / f"xenia-{probe}-smoke"
for name in ("receipt", "hir-receipt", "atomic-receipt", "compiler-receipt", "codegen-receipt", "translation-receipt", "cpu-receipt"):
    items[f"{name}.json"] = root / "build/ps5" / f"{name}.json"
for name, source in {
    "XENIA-BSD": "licenses/XENIA-BSD.txt", "FMT": "licenses/FMT.txt",
    "XBYAK": ".deps/xenia/third_party/xbyak/COPYRIGHT",
    "CAPSTONE": ".deps/xenia/third_party/capstone/LICENSE.TXT",
    "CPPTOML": ".deps/xenia/third_party/cpptoml/LICENSE",
    "CXXOPTS": ".deps/xenia/third_party/cxxopts/LICENSE",
    "DATE": ".deps/xenia/third_party/date/LICENSE.txt",
    "LLVM": ".deps/xenia/third_party/llvm/LICENSE.txt",
    "UTFCPP": ".deps/xenia/third_party/utfcpp/LICENSE",
}.items():
    items[f"licenses/{name}.txt"] = root / source
target = root / "dist/Xbox360PS5-M4-CPU-development-probe.zip"
target.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
    for name, source in items.items():
        archive.write(source, name)
with zipfile.ZipFile(target) as archive:
    if archive.testzip():
        raise SystemExit("Corrupt archive")
print(target)
