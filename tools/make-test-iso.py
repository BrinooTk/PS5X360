"""Packs an extracted game folder into an Xbox 360 disc image (XDVDFS at offset 0),
to test the disc-image path with a game that is known to run from its folder.
Usage: python tools/make-test-iso.py <game folder> <output.iso>"""
import os
import struct
import sys
from pathlib import Path

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"


class Node:
    def __init__(self, path, is_dir):
        self.path, self.is_dir = path, is_dir
        self.name = path.name.encode("cp1252")
        self.children = []
        self.sector = 0
        self.size = 0 if is_dir else path.stat().st_size
        self.table = b""


def scan(path):
    node = Node(path, True)
    for child in sorted(path.iterdir(), key=lambda p: p.name.upper()):
        node.children.append(scan(child) if child.is_dir() else Node(child, False))
    return node


def preorder(entries):
    """A balanced tree of the (sorted) entries, listed root first: (entry, left, right) as list indexes."""
    order = []

    def build(low, high):
        if low >= high:
            return None
        middle = (low + high) // 2
        index = len(order)
        order.append([entries[middle], None, None])
        order[index][1] = build(low, middle)
        order[index][2] = build(middle + 1, high)
        return index

    build(0, len(entries))
    return order


def layout(order):
    """Byte offset of each entry in the directory table; none crosses a sector."""
    offsets, at = [], 0
    for entry, _, _ in order:
        length = (14 + len(entry.name) + 3) & ~3
        if at // SECTOR != (at + length - 1) // SECTOR:
            at = (at // SECTOR + 1) * SECTOR
        offsets.append(at)
        at += length
    return offsets, at


def table_size(node):
    if not node.children:
        return 0
    _, used = layout(preorder(node.children))
    return (used + SECTOR - 1) // SECTOR * SECTOR


def build_table(node):
    order = preorder(node.children)
    offsets, used = layout(order)
    table = bytearray(b"\xff" * ((used + SECTOR - 1) // SECTOR * SECTOR))
    for index, (entry, left, right) in enumerate(order):
        struct.pack_into("<HHIIBB", table, offsets[index],
                         offsets[left] // 4 if left is not None else 0,
                         offsets[right] // 4 if right is not None else 0,
                         entry.sector, entry.size, 0x10 if entry.is_dir else 0x20, len(entry.name))
        table[offsets[index] + 14:offsets[index] + 14 + len(entry.name)] = entry.name
    return bytes(table)


def main():
    source, output = Path(sys.argv[1]), Path(sys.argv[2])
    root = scan(source)
    directories, files = [], []

    def walk(node):
        directories.append(node)
        for child in node.children:
            if child.is_dir:
                walk(child)
            else:
                files.append(child)

    walk(root)
    sector = 33
    for directory in directories:
        directory.size = table_size(directory)
        directory.sector = sector if directory.size else 0
        sector += directory.size // SECTOR
    for file in files:
        file.sector = sector
        sector += (file.size + SECTOR - 1) // SECTOR
    with open(output, "wb") as image:
        image.write(b"\0" * (32 * SECTOR))
        descriptor = bytearray(SECTOR)
        descriptor[0:20] = MAGIC
        struct.pack_into("<II", descriptor, 20, root.sector, root.size)
        descriptor[SECTOR - 20:] = MAGIC
        image.write(descriptor)
        for directory in directories:
            if directory.size:
                assert image.tell() == directory.sector * SECTOR
                image.write(build_table(directory))
        for file in files:
            assert image.tell() == file.sector * SECTOR
            with open(file.path, "rb") as data:
                while True:
                    block = data.read(8 << 20)
                    if not block:
                        break
                    image.write(block)
            image.write(b"\0" * (-file.size % SECTOR))
    print(f"{output}: {len(directories)} folders, {len(files)} files, {os.path.getsize(output)} bytes")


if __name__ == "__main__":
    main()
