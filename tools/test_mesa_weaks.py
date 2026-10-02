"""Ensure null-binding optional Mesa symbols never masks a missing required import."""
from pathlib import Path
import runpy
import unittest
collect = runpy.run_path(str(Path(__file__).with_name('resolve-mesa-weaks.py')))['collect']
class WeakTests(unittest.TestCase):
    def test_required_import_never_altered(self):
        self.assertEqual(collect(' U radv_CreateDevice\n U sceKernelMmap\n w radv_Optional\n'), ['radv_Optional'])
    def test_unrelated_tls_reference_rejected(self):
        with self.assertRaises(ValueError): collect(' w _ZTHThread\n')
    def test_symbol_text_cannot_add_linker_flags(self):
        with self.assertRaises(ValueError): collect(' w radv_;--ignore-all\n')
    def test_only_undefined_weaks_are_collected(self):
        self.assertEqual(collect('000001 W radv_Present\n000002 T radv_Draw\n w wsi_Optional\n'), ['wsi_Optional'])
if __name__ == '__main__': unittest.main()
