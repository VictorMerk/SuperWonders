"""Resource-table growth preserves heap-budget alignment without changing unrelated resources."""
from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from wonder_menu_mod import rstb_grow


class MenuBudgetTests(unittest.TestCase):
    def table(self):
        header = b'RESTBL' + struct.pack('<IIII', 1, 32, 2, 1)
        crcs = b''.join(struct.pack('<II', zlib.crc32(name.encode()), size)
                        for name, size in [('changed', 64), ('unchanged', 128)])
        return header + crcs + b'named'.ljust(32, b'\0') + struct.pack('<I', 96)

    def test_crc_growth_rounds_up_and_preserves_other_entries(self):
        for added in (1, 4, 7, 8):
            with self.subTest(added=added):
                original = self.table()
                growth = {'changed': added}
                out = rstb_grow(original, growth)
                self.assertEqual(struct.unpack_from('<I', out, 26)[0], 72)
                self.assertEqual(out[30:], original[30:])
                self.assertEqual(growth, {'changed': added})

    def test_named_growth_preserves_alignment_and_crc_entries(self):
        original = self.table()
        out = rstb_grow(original, {'named': 13})
        self.assertEqual(out[:-4], original[:-4])
        self.assertEqual(struct.unpack_from('<I', out, len(out) - 4)[0], 112)

    def test_overflow_and_unknown_resource_are_rejected(self):
        with self.assertRaises(ValueError):
            rstb_grow(self.table(), {'changed': 0xFFFFFFFF})
        with self.assertRaises(AssertionError):
            rstb_grow(self.table(), {'missing': 8})


if __name__ == '__main__':
    unittest.main()
