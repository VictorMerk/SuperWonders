"""Synthetic NUP regression checks: bounds, compression, hashes and extraction isolation."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from nup_package import Nup, PackageError


def fixture(path, payload=b'synthetic game-independent bytes', name=b'main', codec=0):
    stored = payload
    if codec:
        import zstandard
        stored = zstandard.ZstdCompressor().compress(payload)
    # Three directories, one file and three child indices. No console data.
    pool = 48 + 64 + 3 * 24 + 80 + 3 * 4
    names = b'exefsromfs' + name
    toc = bytearray(b'TOC\1' + struct.pack('<I4Q', 1, 3, 1, pool, len(names)))
    toc += bytes(48 - len(toc))
    toc += struct.pack('<Q4I', 0x0100000000000000, 0, 1, 2, 0) + bytes(40)
    toc += struct.pack('<4I8x', 0, 0, 0, 2)
    toc += struct.pack('<4I8x', 0, 5, 2, 1)
    toc += struct.pack('<4I8x', 5, 5, 3, 0)
    toc += struct.pack('<II3Q', 10, len(name), len(payload), 4096, len(stored))
    toc += hashlib.sha256(payload).digest() + bytes([codec]) + bytes(15)
    toc += struct.pack('<3I', 1, 2, 3) + names
    header = (b'NUP\1' + struct.pack('<II', 0x10080, 128)).ljust(128, b'\0')
    path.write_bytes((header + toc).ljust(4096, b'\0') + stored)
    return payload


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.path = self.root / 'source.nup'

    def test_raw_read_and_hash(self):
        expected = fixture(self.path)
        package = Nup(self.path)
        self.assertEqual(package.read(package.entries('exefs')['main']), expected)
        self.assertEqual(package.entries('romfs'), {})

    def test_compressed_read(self):
        try:
            import zstandard
        except ImportError:
            self.skipTest('zstandard unavailable')
        expected = fixture(self.path, codec=1)
        package = Nup(self.path)
        self.assertEqual(package.read(package.entries('exefs')['main']), expected)

    def test_hash_corruption_is_rejected(self):
        fixture(self.path)
        data = bytearray(self.path.read_bytes()); data[-1] ^= 1; self.path.write_bytes(data)
        package = Nup(self.path)
        with self.assertRaisesRegex(PackageError, 'SHA-256'):
            package.read(package.entries('exefs')['main'])

    def test_truncated_toc_is_rejected(self):
        fixture(self.path)
        self.path.write_bytes(self.path.read_bytes()[:200])
        with self.assertRaises(PackageError):
            Nup(self.path)

    def test_oversized_toc_is_rejected(self):
        fixture(self.path)
        data = bytearray(self.path.read_bytes()); struct.pack_into('<Q', data, 128 + 24, 1 << 40)
        self.path.write_bytes(data)
        with self.assertRaises(PackageError):
            Nup(self.path)

    def test_unsafe_names_are_rejected(self):
        for name in (b'../main', b'C:main', b'..', b'CON', b'main\\bad', b'main.'):
            with self.subTest(name=name):
                fixture(self.path, name=name)
                with self.assertRaises(PackageError):
                    Nup(self.path).entries('exefs')

    def test_directory_cycle_is_rejected(self):
        fixture(self.path)
        data = bytearray(self.path.read_bytes())
        struct.pack_into('<I', data, 128 + 48 + 64 + 3 * 24 + 80 + 8, 1)
        self.path.write_bytes(data)
        with self.assertRaisesRegex(PackageError, 'Cyclic'):
            Nup(self.path).entries('exefs')

    def test_invalid_child_is_rejected(self):
        fixture(self.path)
        data = bytearray(self.path.read_bytes())
        struct.pack_into('<I', data, 128 + 48 + 64 + 3 * 24 + 80 + 8, 99)
        self.path.write_bytes(data)
        with self.assertRaisesRegex(PackageError, 'child index'):
            Nup(self.path).entries('exefs')

    def test_extraction_never_replaces_existing_directory(self):
        expected = fixture(self.path)
        package = Nup(self.path); out = self.root / 'exefs'
        self.assertEqual(package.extract('exefs', out), 1)
        self.assertEqual((out / 'main').read_bytes(), expected)
        with self.assertRaisesRegex(PackageError, 'new output directory'):
            package.extract('exefs', out)
        self.assertEqual((out / 'main').read_bytes(), expected)

    def test_read_limit_is_enforced(self):
        fixture(self.path)
        package = Nup(self.path)
        with self.assertRaisesRegex(PackageError, 'read limit'):
            package.read(package.entries('exefs')['main'], max_size=1)

    def test_invalid_title_or_part_is_rejected(self):
        fixture(self.path); package = Nup(self.path)
        for part, index in (('exefs', -1), ('exefs', 1), ('anything', 0)):
            with self.assertRaises(PackageError):
                package.entries(part, index)


if __name__ == '__main__':
    unittest.main()
