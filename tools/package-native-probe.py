"""Validate and archive the first native PS5 hardware test."""
import hashlib
import json
from pathlib import Path
import struct
import zipfile
import argparse

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--cpu-translation", action="store_true")
cpu = parser.parse_args().cpu_translation
title_id = "PPSA50009" if cpu else "PPSA50008"
stage = root / ("build/native-cpu-stage" if cpu else "build/native-stage")
app = stage / "dist" / title_id
param = json.loads((app / "sce_sys/param.json").read_text())
if param["titleId"] != title_id:
    raise SystemExit("Wrong probe title identity")
if struct.unpack_from("<I", (app / "eboot.bin").read_bytes())[0] != 0x1D3D154F:
    raise SystemExit("Missing native FSELF header")
if not (app / "sce_module/libc.prx").is_file():
    raise SystemExit("Runtime module missing")
receipt = {
    "title_id": title_id, "milestone": "M4" if cpu else "M1",
    "purpose": "Native Xenia decoder, VideoOut and DualSense hardware probe",
    "hardware_tested": False, "plays_games": False,
    "build": json.loads((stage / "stage-receipt.json").read_text()),
    "files": {str(p.relative_to(app)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in app.rglob("*") if p.is_file()},
}
output = root / "dist/Xbox360PS5-M1-native-platform-test.zip"
if cpu:
    receipt["purpose"] = "Native synthetic PPC emitter/HIR oracle and optimizer probe; no guest runtime or JIT"
    output = root / "dist/Xbox360PS5-M4-native-cpu-test.zip"
evidence_path = root / ("docs/evidence/M4_HARDWARE.json" if cpu else "docs/evidence/M1_HARDWARE.json")
if evidence_path.exists():
    evidence = json.loads(evidence_path.read_text())
    same_runtime = not cpu or evidence.get("libc_sha256") == receipt["files"]["sce_module/libc.prx"]
    if evidence.get("eboot_sha256") == receipt["files"]["eboot.bin"] and same_runtime:
        receipt["hardware_tested"] = True
        receipt["hardware_evidence"] = evidence
output.parent.mkdir(parents=True, exist_ok=True)
(output.parent / ("native-cpu-probe-receipt.json" if cpu else "native-probe-receipt.json")).write_text(json.dumps(receipt, indent=2) + "\n")
with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
    for path in app.rglob("*"):
        if path.is_file(): archive.write(path, title_id + "/" + str(path.relative_to(app)).replace("\\", "/"))
    archive.writestr("receipt.json", json.dumps(receipt, indent=2) + "\n")
    for path in (root / "licenses").glob("*"):
        archive.write(path, "licenses/" + path.name)
    archive.write(root / ("docs/CPU_NATIVE_TEST.md" if cpu else "docs/FIRST_TEST.md"), "FIRST_TEST.md")
    if cpu:
        for name, source in {"CPPTOML": "cpptoml/LICENSE", "CXXOPTS": "cxxopts/LICENSE",
                             "UTFCPP": "utfcpp/LICENSE", "LLVM": "llvm/LICENSE.txt"}.items():
            archive.write(root / ".deps/xenia/third_party" / source, f"licenses/{name}.txt")
with zipfile.ZipFile(output) as archive:
    if archive.testzip(): raise SystemExit("Corrupt test ZIP")
print(f"Native platform test ready: {output}")
