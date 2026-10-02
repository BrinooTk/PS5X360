"""Validate and archive the first native PS5 hardware test."""
import hashlib
import json
from pathlib import Path
import struct
import zipfile

root = Path(__file__).resolve().parents[1]
stage = root / "build/native-stage"
app = stage / "dist/PPSA50008"
param = json.loads((app / "sce_sys/param.json").read_text())
if param["titleId"] != "PPSA50008":
    raise SystemExit("Wrong probe title identity")
if struct.unpack_from("<I", (app / "eboot.bin").read_bytes())[0] != 0x1D3D154F:
    raise SystemExit("Missing native FSELF header")
if not (app / "sce_module/libc.prx").is_file():
    raise SystemExit("Runtime module missing")
receipt = {
    "title_id": "PPSA50008", "milestone": "M1",
    "purpose": "Native Xenia decoder, VideoOut and DualSense hardware probe",
    "hardware_tested": False, "plays_games": False,
    "build": json.loads((stage / "stage-receipt.json").read_text()),
    "files": {str(p.relative_to(app)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in app.rglob("*") if p.is_file()},
}
output = root / "dist/Xbox360PS5-M1-native-platform-test.zip"
evidence_path = root / "docs/evidence/M1_HARDWARE.json"
if evidence_path.exists():
    evidence = json.loads(evidence_path.read_text())
    if evidence.get("eboot_sha256") == receipt["files"]["eboot.bin"]:
        receipt["hardware_tested"] = True
        receipt["hardware_evidence"] = evidence
output.parent.mkdir(parents=True, exist_ok=True)
(output.parent / "native-probe-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
    for path in app.rglob("*"):
        if path.is_file(): archive.write(path, "PPSA50008/" + str(path.relative_to(app)).replace("\\", "/"))
    archive.writestr("receipt.json", json.dumps(receipt, indent=2) + "\n")
    for path in (root / "licenses").glob("*"):
        archive.write(path, "licenses/" + path.name)
    archive.write(root / "docs/FIRST_TEST.md", "FIRST_TEST.md")
with zipfile.ZipFile(output) as archive:
    if archive.testzip(): raise SystemExit("Corrupt test ZIP")
print(f"Native platform test ready: {output}")
