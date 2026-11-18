"""A generated library may be built only after each module reports complete CPU lifting."""
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import gamebuild


class CoverageTests(unittest.TestCase):
    def run_report(self, report):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        gen = root / 'game/work/gen'
        (gen / 'main').mkdir(parents=True)
        (gen / 'main/report.txt').write_text(report, encoding='utf-8')
        process = SimpleNamespace(stdout=iter(['done\n']), wait=lambda: 0)
        with patch.object(gamebuild, 'MODULES', [('main', '0x4000000000')]), \
             patch.object(gamebuild.subprocess, 'Popen', return_value=process):
            gamebuild.recompile(SimpleNamespace(root=str(root)), 'source', str(gen), 0, 1)
        return root / 'game/recomp-main-report.txt'

    def test_complete_lifting_preserves_coverage_after_work_cleanup(self):
        text = 'functions 10, handled 100, unhandled 0\njump tables 0\n'
        report = self.run_report(text)
        self.assertEqual(report.read_text(encoding='utf-8'), text)

    def test_successful_generator_with_unhandled_instruction_is_rejected(self):
        with self.assertRaisesRegex(SystemExit, 'unhandled CPU instructions'):
            self.run_report('functions 10, handled 99, unhandled 1\n1 unsupported\n')

    def test_missing_coverage_verdict_is_rejected(self):
        with self.assertRaisesRegex(SystemExit, 'unhandled CPU instructions'):
            self.run_report('generator output without a coverage verdict\n')


if __name__ == '__main__':
    unittest.main()
