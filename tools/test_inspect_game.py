import struct
import unittest
from inspect_game import inspect_xex


def fixture():
    raw = bytearray(0x410)
    struct.pack_into(">6I", raw, 0, 0x58455832, 1, 0x400, 0, 0x80, 4)
    for n, pair in enumerate(((0x10100, 0x82000020), (0x10201, 0x82000000),
                              (0x3FF, 0x220), (0x103FF, 0x240))):
        struct.pack_into(">2I", raw, 24 + n * 8, *pair)
    struct.pack_into(">2I", raw, 0x80, 0x19C, 0x1000)
    struct.pack_into(">I", raw, 0x80 + 0x110, 0x82000000)
    struct.pack_into(">I", raw, 0x80 + 0x180, 1)
    struct.pack_into(">I2H", raw, 0x220, 8, 0, 0)
    struct.pack_into(">3I", raw, 0x240, 64, 8, 1)
    raw[0x24C:0x254] = b"xam.xex\0"
    struct.pack_into(">I", raw, 0x254, 44)
    struct.pack_into(">2H", raw, 0x254 + 36, 0, 1)
    return raw


class InspectGameTests(unittest.TestCase):
    def test_valid_metadata(self):
        report = inspect_xex(fixture())
        self.assertEqual(report["entry_point"], "0x82000020")
        self.assertEqual(report["import_libraries"], [{"name": "xam.xex", "records": 1}])
        self.assertFalse(report["content_executed"])

    def test_truncation_and_magic(self):
        for raw in (b"", b"XEX2", b"ELF2" + bytes(32)):
            with self.subTest(raw=raw), self.assertRaises(ValueError): inspect_xex(raw)

    def test_bad_ranges(self):
        for offset, value in ((8, 0xFFFFFFF0), (16, 0xFFFFFFF0), (20, 0x1001),
                              (0x80, 0x183), (0x80 + 0x180, 0xFFFFFFFF),
                              (28, 0x92000000), (52, 0xFFFFFFF0)):
            raw = fixture(); struct.pack_into(">I", raw, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError): inspect_xex(raw)

    def test_duplicate_header(self):
        raw = fixture(); struct.pack_into(">I", raw, 32, 0x10100)
        with self.assertRaises(ValueError): inspect_xex(raw)

    def test_import_name_termination(self):
        raw = fixture(); raw[0x24C:0x254] = b"abcdefgh"
        with self.assertRaises(ValueError): inspect_xex(raw)

    def test_import_name_index(self):
        raw = fixture(); struct.pack_into(">H", raw, 0x254 + 36, 1)
        with self.assertRaises(ValueError): inspect_xex(raw)

    def test_import_record_bounds(self):
        raw = fixture(); struct.pack_into(">H", raw, 0x254 + 38, 2)
        with self.assertRaises(ValueError): inspect_xex(raw)

    def test_short_nested_headers(self):
        for offset, value in ((0x220, 4), (0x240, 8), (0x248, 0xffffffff)):
            raw = fixture(); struct.pack_into(">I", raw, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError): inspect_xex(raw)


if __name__ == "__main__": unittest.main()
