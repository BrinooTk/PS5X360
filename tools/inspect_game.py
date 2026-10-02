"""Read-only XEX2 preflight. Does not decrypt, load, patch or execute content."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect_xex(raw):
    def block(offset, length):
        if offset < 0 or length < 0 or offset > len(raw) or length > len(raw) - offset:
            raise ValueError("XEX range exceeds file")
        return raw[offset:offset + length]

    def u32(offset):
        return struct.unpack(">I", block(offset, 4))[0]

    if block(0, 4) != b"XEX2":
        raise ValueError("Expected XEX2")
    _, flags, image_offset, _, security, count = struct.unpack(">6I", block(0, 24))
    if count > 1024 or image_offset >= len(raw) or 24 + count * 8 > image_offset:
        raise ValueError("Invalid XEX header table")
    headers = {}
    for index in range(count):
        key, value = struct.unpack(">2I", block(24 + index * 8, 8))
        if key in headers:
            raise ValueError("Duplicate XEX optional header")
        low = key & 255
        if low not in (0, 1):
            length = u32(value) if low == 255 else low * 4
            if (low == 255 and length < 4) or value < 24 + count * 8 or value + length > image_offset:
                raise ValueError("Invalid optional header range")
            block(value, length)
        headers[key] = value
    if security < 24 + count * 8:
        raise ValueError("Invalid security info offset")
    security_size = u32(security)
    if security_size < 0x184 or security + security_size > image_offset:
        raise ValueError("Invalid security info size")
    block(security, security_size)
    pages = u32(security + 0x180)
    if 0x184 + pages * 24 > security_size:
        raise ValueError("Invalid page descriptor table")
    image_size = u32(security + 4)
    image_base = headers.get(0x10201, u32(security + 0x110))
    entry = headers.get(0x10100)
    if not image_size or image_base + image_size > 0x100000000:
        raise ValueError("Invalid guest image range")
    if entry is not None and not image_base <= entry < image_base + image_size:
        raise ValueError("Entry point is outside guest image")
    result = {"format": "XEX2", "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
              "module_flags": flags, "image_offset": image_offset, "image_size": image_size,
              "image_base": f"0x{image_base:08X}", "entry_point": f"0x{entry:08X}" if entry is not None else None,
              "optional_headers": count, "page_descriptors": pages,
              "content_executed": False, "content_decrypted": False}
    execution = headers.get(0x40006)
    if execution is not None:
        result["title_id"] = f"{u32(execution + 12):08X}"
    file_format = headers.get(0x3FF)
    if file_format is not None:
        if u32(file_format) < 8:
            raise ValueError("Truncated file format header")
        result["encryption"], result["compression"] = struct.unpack(">2H", block(file_format + 4, 4))
    imports = headers.get(0x103FF)
    if imports is not None:
        if u32(imports) < 12:
            raise ValueError("Truncated import header")
        size, strings_size, names_count = struct.unpack(">3I", block(imports, 12))
        if strings_size > size - 12:
            raise ValueError("Invalid import string table")
        strings = block(imports + 12, strings_size)
        if names_count > strings_size:
            raise ValueError("Invalid import name count")
        names, cursor = [], 0
        for _ in range(names_count):
            end = strings.find(b"\0", cursor)
            if end < 0:
                raise ValueError("Unterminated import name")
            names.append(strings[cursor:end].decode("ascii"))
            cursor = (end + 4) & ~3
        libraries, cursor = [], 12 + strings_size
        while cursor < size:
            record = imports + cursor
            length = u32(record)
            if length < 40 or length > size - cursor:
                raise ValueError("Invalid import library size")
            name_index, count = struct.unpack(">2H", block(record + 36, 4))
            name_index &= 255
            if name_index >= len(names) or count * 4 > length - 40:
                raise ValueError("Invalid import record table")
            libraries.append({"name": names[name_index], "records": count})
            cursor += length
        result["import_libraries"] = libraries
        result["import_library_count"] = len(libraries)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    executable = args.path / "default.xex" if args.path.is_dir() else args.path
    result = inspect_xex(executable.read_bytes())
    if args.path.is_dir():
        files = [p for p in args.path.rglob("*") if p.is_file()]
        result["content_files"] = len(files)
        result["content_bytes"] = sum(p.stat().st_size for p in files)
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    print(text)


if __name__ == "__main__":
    main()
