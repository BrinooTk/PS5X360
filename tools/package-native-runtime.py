"""Validate and package the M7 actual CPU runtime native title, not a game."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
STAGE = ROOT / "build/native-runtime-stage"
APP = STAGE / "dist/PPSA50010"
param = json.loads((APP / "sce_sys/param.json").read_text())
if param["titleId"] != "PPSA50010": raise SystemExit("Wrong native runtime identity")
log = (ROOT / "build/m7-host.log").read_text()
xml = (ROOT / "build/runtime-host/runtime-tests.xml").read_text()
if "100% tests passed, 0 tests failed out of 8" not in log or "RUNTIME REINITIALIZATION RUNS 2 FAILURES 0" not in xml:
    raise SystemExit("Passing host runtime and reinitialization evidence required")
build_log = (ROOT / "build/native-runtime.log").read_text()
if "NATIVE MEMORY BRIDGE CASES 7 FAILURES 0" not in build_log or "Build complete." not in build_log:
    raise SystemExit("Native bridge test and successful native build required")
subprocess.check_call([sys.executable, str(STAGE / "tools/verify-image.py"), str(STAGE / "build/llvm-pie.elf"), str(STAGE / "build/eboot.elf")])
tool = STAGE / "build/host/ps5-native-tool"
for path in (APP / "eboot.bin", APP / "sce_module/libc.prx"):
    subprocess.check_call([str(tool), "self", "--inspect", "--file", str(path)])
sources = ["src/runtime_link_gate.cpp", "platform/ps5/native_runtime_probe.cpp",
           "platform/ps5/native_memory_calls.cpp", "src/native_memory_bridge_contract.cpp",
           "tools/native-stage.py", "tools/build-native-runtime-probe.sh"]
report = {"stage": "M7 actual CPU native runtime", "title_id": "PPSA50010",
          "hardware_tested": False, "plays_games": False, "vulkan_initialized": False,
          "host_jit_runs": 2, "host_jit_checks_per_run": 50, "native_bridge_checks": 7,
          "files": {path.relative_to(APP).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in APP.rglob("*") if path.is_file()},
          "sources": {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in sources},
          "build": json.loads((STAGE / "stage-receipt.json").read_text())}
evidence = ROOT / "docs/evidence/M7_NATIVE_RUNTIME.json"
evidence.write_text(json.dumps(report, indent=2) + "\n")
destination = ROOT / "dist/Xbox360PS5-M7-native-runtime-test.zip"
with zipfile.ZipFile(destination,"w",zipfile.ZIP_DEFLATED) as package:
    for path in sorted(APP.rglob("*")):
        if path.is_file(): package.write(path,"PPSA50010/" + path.relative_to(APP).as_posix())
    package.write(evidence,"receipt.json")
    package.write(ROOT / "docs/ACTUAL_CPU_NATIVE_TEST.md","FIRST_TEST.md")
    for path in (ROOT / "licenses").glob("*"):
        if path.is_file(): package.write(path,"licenses/" + path.name)
    for name, relative in {"CAPSTONE": "capstone/LICENSE.TXT", "XBYAK": "xbyak/COPYRIGHT",
                           "LLVM": "llvm/LICENSE.txt", "CPPTOML": "cpptoml/LICENSE",
                           "CXXOPTS": "cxxopts/LICENSE", "UTFCPP": "utfcpp/LICENSE"}.items():
        package.write(ROOT / ".deps/xenia/third_party" / relative,"licenses/" + name + ".txt")
with zipfile.ZipFile(destination) as package:
    if package.testzip(): raise SystemExit("Corrupt native runtime ZIP")
print(json.dumps({"package":str(destination),"sha256":hashlib.sha256(destination.read_bytes()).hexdigest(),"bytes":destination.stat().st_size,"hardware_tested":False,"plays_games":False}))
