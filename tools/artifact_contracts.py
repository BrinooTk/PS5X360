"""Structural validation shared by the CPU and runtime development packages."""
import hashlib
import struct


def check_archive(path):
    raw = path.read_bytes()
    if raw[:8] != b"!<arch>\n":
        raise SystemExit(f"Not a regular archive: {path}")
    offset, objects = 8, 0
    while offset < len(raw):
        header = raw[offset:offset + 60]
        if len(header) != 60 or header[58:60] != b"`\n":
            raise SystemExit(f"Invalid archive header: {path}")
        size = int(header[48:58])
        if size < 0: raise SystemExit(f"Negative archive member size: {path}")
        payload = raw[offset + 60:offset + 60 + size]
        if len(payload) != size:
            raise SystemExit(f"Truncated archive: {path}")
        if header[:16].strip() not in (b"/", b"//", b"/SYM64/"):
            if payload[:7] != b"\x7fELF\x02\x01\x01" or len(payload) < 64:
                raise SystemExit(f"Expected ELF64 object in {path}")
            if struct.unpack_from("<HH", payload, 16) != (1, 62):
                raise SystemExit(f"Expected x86-64 relocatable object in {path}")
            objects += 1
        offset += 60 + size + (size & 1)
    if offset != len(raw) or not objects:
        raise SystemExit(f"Invalid archive end: {path}")
    return {"objects": objects, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
