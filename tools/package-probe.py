"""Package the validated development probe, with its notices and receipt."""
from pathlib import Path
import subprocess
import sys
import zipfile

root = Path(__file__).resolve().parents[1]
subprocess.check_call([sys.executable, str(root / "tools/verify.py")])
target = root / "dist/Xbox360PS5-M0-development-probe.zip"
target.parent.mkdir(parents=True, exist_ok=True)
items = {
    "xenia-platform-smoke.elf": "build/ps5/xenia-platform-smoke",
    "libxenia_ppc_decoder.a": "build/ps5/libxenia_ppc_decoder.a",
    "receipt.json": "build/ps5/receipt.json",
    "README.md": "README.md",
    "PORTING.md": "docs/PORTING.md",
    "LICENSE": "LICENSE",
    "licenses/XENIA-BSD.txt": "licenses/XENIA-BSD.txt",
    "licenses/FMT.txt": "licenses/FMT.txt",
}
with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
    for name, source in items.items():
        archive.write(root / source, name)
with zipfile.ZipFile(target) as archive:
    bad = archive.testzip()
    if bad:
        raise SystemExit(f"Corrupt archive member: {bad}")
print(target)
