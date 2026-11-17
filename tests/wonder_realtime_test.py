"""Validate the upstream camera/frame-step patch with synthetic instruction sites."""
from pathlib import Path
import importlib.util
import struct
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location("wonder_realtime", Path(__file__).resolve().parents[1] / "recomp/wonder_realtime.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)

class RealtimePatch(unittest.TestCase):
    def module(self):
        size = patch.ASPECT_SITE + 0x100
        image = bytearray(size)
        for at in patch.BRANCHES:
            struct.pack_into("<8I", image, at, *patch.ORIGINAL)
        struct.pack_into("<4I", image, patch.ASPECT_SITE, *patch.ASPECT_ORIGINAL)
        return SimpleNamespace(name="main", text=(0, size), bss=(size + 0x100, 0x1234), image=image)

    def test_camera_and_frame_step_share_the_reserved_page(self):
        module = self.module()
        patch.patch_wonder_realtime(module)
        page = module.frame_step_offset
        self.assertEqual(page % 4096, 0)
        self.assertEqual(sum(module.bss), page + 4096)
        for at, register in [(p, 8) for p in patch.BRANCHES] + [(patch.ASPECT_SITE, 9)]:
            instruction = struct.unpack_from("<I", module.image, at)[0]
            self.assertEqual(instruction & 31, register)
            immediate = ((instruction >> 29) & 3) | (((instruction >> 5) & 0x7ffff) << 2)
            if immediate & (1 << 20): immediate -= 1 << 21
            self.assertEqual((at & ~4095) + immediate * 4096, page)
        words = struct.unpack_from("<4I", module.image, patch.ASPECT_SITE)
        self.assertEqual(words[1:], (patch.ASPECT_ORIGINAL[1], 0xBD400920, patch.ASPECT_ORIGINAL[3]))

    def test_unexpected_frame_instructions_fail_closed(self):
        module = self.module()
        struct.pack_into("<I", module.image, patch.BRANCHES[0], 0)
        with self.assertRaisesRegex(ValueError, "frame-step patch expects game version"):
            patch.patch_wonder_realtime(module)

    def test_unexpected_camera_instructions_fail_closed(self):
        module = self.module()
        struct.pack_into("<I", module.image, patch.ASPECT_SITE, 0)
        with self.assertRaisesRegex(ValueError, "perspective-aspect patch expects game version"):
            patch.patch_wonder_realtime(module)

    def test_camera_site_outside_text_is_rejected(self):
        module = self.module()
        module.text = (0, patch.ASPECT_SITE)
        with self.assertRaisesRegex(ValueError, "outside main text"):
            patch.patch_wonder_realtime(module)

    def test_other_modules_are_untouched(self):
        module = SimpleNamespace(name="sdk", image=b"synthetic sdk")
        patch.patch_wonder_realtime(module)
        self.assertEqual(module.image, b"synthetic sdk")
        self.assertFalse(hasattr(module, "frame_step_offset"))

if __name__ == "__main__": unittest.main()
