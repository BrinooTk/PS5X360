"""Validate the actual cross-built artifact and write its provenance."""
import hashlib
import json
from pathlib import Path
import struct
import sys

root = Path(__file__).resolve().parents[1]
artifact = sys.argv[1] if len(sys.argv) > 1 else "xenia-platform-smoke"
probes = {
    "xenia-platform-smoke": ("instruction-decoder", "receipt.json"),
    "xenia-hir-smoke": ("HIR semantics", "hir-receipt.json"),
    "xenia-atomic-smoke": ("PS5 atomic semantics", "atomic-receipt.json"),
    "xenia-compiler-smoke": ("optimizer semantics", "compiler-receipt.json"),
    "xenia-codegen-smoke": ("x64 encoding/decoding", "codegen-receipt.json"),
}
if artifact not in probes:
    raise SystemExit("Unknown probe artifact")
path = root / "build/ps5" / artifact
raw = path.read_bytes()
if raw[:7] != b"\x7fELF\x02\x01\x01":
    raise SystemExit("Expected a little-endian ELF64")
if struct.unpack_from("<H", raw, 18)[0] != 62:
    raise SystemExit("Expected x86-64")
phoff = struct.unpack_from("<Q", raw, 32)[0]
phsize, phnum = struct.unpack_from("<HH", raw, 54)
entry = struct.unpack_from("<Q", raw, 24)[0]
if phsize != 56 or phoff + phsize * phnum > len(raw):
    raise SystemExit("Invalid ELF program header table")
entry_mapped = False
segments = []
for i in range(phnum):
    kind, flags, offset, address, _, filesz, memsz, alignment = struct.unpack_from(
        "<IIQQQQQQ", raw, phoff + i * phsize)
    if offset + filesz > len(raw):
        raise SystemExit("ELF segment exceeds file bounds")
    if kind == 1:
        if filesz > memsz or (alignment > 1 and offset % alignment != address % alignment):
            raise SystemExit("Invalid ELF load segment layout")
        entry_mapped |= bool(flags & 1 and address <= entry < address + memsz)
        segments.append({"flags": flags, "bytes": memsz})
if not entry_mapped:
    raise SystemExit("Entry point is not in an executable load segment")
receipt = {
    "artifact": path.name,
    "sha256": hashlib.sha256(raw).hexdigest(),
    "bytes": len(raw),
    "dependencies": json.loads((root / "deps.json").read_text()),
    "status": "PS5 cross-built " + probes[artifact][0] + " probe; not a playable emulator",
    "hardware_tested": False,
    "format": "public payload SDK ELF; not a registered native title/FSELF",
    "load_segments": segments,
}
(path.parent / probes[artifact][1]).write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps({key: receipt[key] for key in ("artifact", "bytes", "sha256", "status")}))
