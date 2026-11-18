"""A blank image with a metrics overlay must not count as observed game content."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'perf'))
from check_wonder_captures import check_captures, frame_content


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def write(self, name, pixel):
        path = self.root / name
        data = bytes(v for y in range(128) for x in range(128) for v in pixel(x, y))
        path.write_bytes(b'P6\n128 128\n255\n' + data)
        return path

    def test_metrics_overlay_does_not_hide_blank_content(self):
        self.write('00001.ppm', lambda x, y: (x, y, x ^ y) if x < 24 and y < 24 else (20, 30, 20))
        self.assertFalse(check_captures(self.root)['nonflat_content_observed'])

    def test_solid_colour_is_flat_even_with_different_rgb_channels(self):
        path = self.write('00001.ppm', lambda x, y: (255, 0, 128))
        self.assertFalse(frame_content(path)['nonflat'])

    def test_real_content_can_follow_a_flat_loading_frame(self):
        self.write('00001.ppm', lambda x, y: (0, 0, 0))
        self.write('00002.ppm', lambda x, y: (x, y, x ^ y))
        self.assertTrue(check_captures(self.root)['nonflat_content_observed'])

    def test_invalid_or_missing_frames_fail(self):
        with self.assertRaises(ValueError):
            check_captures(self.root)
        path = self.write('00001.ppm', lambda x, y: (1, 2, 3))
        path.write_bytes(path.read_bytes()[:-1])
        with self.assertRaises(ValueError):
            frame_content(path)


if __name__ == '__main__':
    unittest.main()
