"""Ensure FTP metadata normalization cannot hide corruption in native code."""
import struct
import unittest
import runpy
from pathlib import Path
verify_eboot = runpy.run_path(str(Path(__file__).with_name("deploy-native-probe.py")))["verify_eboot"]


class VerificationTests(unittest.TestCase):
    def setUp(self):
        self.elf = bytearray(512)
        self.elf[:4] = b"\x7fELF"
        struct.pack_into("<Q", self.elf, 32, 64)
        struct.pack_into("<HH", self.elf, 54, 56, 2)
        struct.pack_into("<IIQQQQQQ", self.elf, 64, 1, 5, 256, 0x4000, 0x4000, 32, 32, 16)
        struct.pack_into("<IIQQQQQQ", self.elf, 120, 0x6FFFFF01, 0, 320, 0, 0, 16, 16, 1)
        self.elf[256:288] = b"C" * 32
        self.elf[320:336] = b"M" * 16
        self.remote = bytearray(self.elf)
        self.remote[320:336] = bytes(16)

    def verify(self):
        return verify_eboot(bytes(self.remote), b"signed-self", bytes(self.elf))

    def test_only_zeroed_unmapped_notes_allowed(self):
        self.assertIn("stripped 1", self.verify())

    def test_code_corruption_rejected(self):
        self.remote[256] ^= 1
        with self.assertRaises(RuntimeError): self.verify()

    def test_header_corruption_rejected(self):
        self.remote[70] ^= 1
        with self.assertRaises(RuntimeError): self.verify()

    def test_nonzero_note_mutation_rejected(self):
        self.remote[320] = 1
        with self.assertRaises(RuntimeError): self.verify()


if __name__ == "__main__":
    unittest.main()
