"""Pure geometry checks; no Tk window, process launch, game data or GPU."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "wonder_library_layout", Path(__file__).parents[1] / "tools/wonder_library.py")
library = importlib.util.module_from_spec(spec)
spec.loader.exec_module(library)


class LibraryLayoutTests(unittest.TestCase):
    def test_client_and_minimum_fit_available_desktop(self):
        for area in ((0, 0, 640, 440), (0, 0, 1366, 728), (0, 0, 3840, 2080),
                     (-1920, 0, 0, 1040), (0, -1080, 1920, 0), (1800, 300, 2600, 900)):
            with self.subTest(area=area):
                width, height, x, y, minimum_w, minimum_h = library.library_window_geometry(area)
                self.assertGreater(width, 0)
                self.assertGreater(height, 0)
                self.assertLessEqual(minimum_w, width)
                self.assertLessEqual(minimum_h, height)
                self.assertGreaterEqual(x, area[0])
                self.assertGreaterEqual(y, area[1])
                self.assertLessEqual(x + width + 16, area[2])
                self.assertLessEqual(y + height + 40, area[3])

    def test_preferred_size_and_small_desktop_minimum(self):
        self.assertEqual(library.library_window_geometry((0, 0, 1920, 1040))[:2], (960, 740))
        small = library.library_window_geometry((0, 0, 480, 360))
        self.assertEqual(small[:2], (448, 296))
        self.assertEqual(small[-2:], (448, 296))

    def test_card_breakpoint_respects_actual_action_font_width(self):
        self.assertTrue(library.library_card_stacked(500, 180, 14))
        self.assertFalse(library.library_card_stacked(800, 180, 14))
        self.assertTrue(library.library_card_stacked(800, 300, 22))
        self.assertFalse(library.library_card_stacked(1100, 300, 22))
        self.assertTrue(library.library_card_stacked(0, 180, 14))


if __name__ == '__main__':
    unittest.main()
