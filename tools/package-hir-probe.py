"""Package the CPU HIR development gate; never an installable title."""
from pathlib import Path
import subprocess
import sys
import zipfile

root = Path(__file__).resolve().parents[1]
subprocess.check_call([sys.executable, str(root / "tools/verify.py"), "xenia-hir-smoke"])
target = root / "dist/Xbox360PS5-M2-HIR-development-probe.zip"
target.parent.mkdir(parents=True, exist_ok=True)
items = {
    "xenia-hir-smoke.elf": "build/ps5/xenia-hir-smoke",
    "libxenia_hir_values.a": "build/ps5/libxenia_hir_values.a",
    "libxenia_ppc_decoder.a": "build/ps5/libxenia_ppc_decoder.a",
    "hir-receipt.json": "build/ps5/hir-receipt.json",
    "CPU_TRANSLATION.md": "docs/CPU_TRANSLATION.md",
    "LICENSE": "LICENSE",
    "licenses/XENIA-BSD.txt": "licenses/XENIA-BSD.txt",
    "licenses/FMT.txt": "licenses/FMT.txt",
}
with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
    for name, source in items.items():
        archive.write(root / source, name)
with zipfile.ZipFile(target) as archive:
    if archive.testzip():
        raise SystemExit("Corrupt development archive")
print(target)
