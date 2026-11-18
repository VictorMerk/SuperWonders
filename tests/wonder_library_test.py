"""Edition launch boundaries, using synthetic executables without starting a game."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from wonder_library import default_config, launch_spec, load_config, save_config


class LibraryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.executable = self.root / 'Wonder & flower [test].exe'
        self.executable.write_bytes(b'synthetic executable placeholder')
        self.data = self.root / 'original data'; self.data.mkdir()
        self.config = default_config()
        self.config['editions']['original'] = {'executable': str(self.executable), 'data_root': str(self.data)}

    def test_argument_paths_are_passed_without_a_shell_and_qa_overrides_removed(self):
        with patch.dict(os.environ, {'NX_TEST_QUIT_FRAME': '1', 'NX_GAMELIB': 'wrong.dll', 'NX_DATA_ROOT': 'wrong'}):
            argv, cwd, env = launch_spec(self.config, 'original')
        self.assertEqual(argv, [str(self.executable.resolve())])
        self.assertEqual(cwd, str(self.root.resolve()))
        self.assertEqual(env['NX_DATA_ROOT'], str(self.data.resolve()))
        self.assertNotIn('NX_GAMELIB', env)
        self.assertNotIn('NX_TEST_QUIT_FRAME', env)

    def test_unconfigured_switch2_does_not_launch(self):
        with self.assertRaisesRegex(ValueError, 'prepared runtime'):
            launch_spec(self.config, 'switch2')

    def test_shared_save_root_is_rejected(self):
        self.config['editions']['switch2']['data_root'] = str(self.data / '..' / self.data.name)
        with self.assertRaisesRegex(ValueError, 'separate game-data'):
            launch_spec(self.config, 'original')

    def test_missing_runtime_is_rejected(self):
        self.executable.unlink()
        with self.assertRaisesRegex(ValueError, 'executable cannot'):
            launch_spec(self.config, 'original')

    def test_package_is_not_an_executable(self):
        package = self.root / 'game.nup'; package.write_bytes(b'NUP\1')
        self.config['editions']['original']['executable'] = str(package)
        with self.assertRaisesRegex(ValueError, 'package cannot'):
            launch_spec(self.config, 'original')

    def test_configuration_round_trip_and_separate_editions(self):
        p = self.root / 'library.json'; save_config(p, self.config)
        loaded = load_config(p)
        self.assertEqual(loaded, self.config)
        self.assertEqual(set(loaded['editions']), {'original', 'switch2'})
        self.assertEqual(loaded['editions']['switch2']['executable'], '')

    def test_unknown_configuration_is_not_overwritten(self):
        p = self.root / 'library.json'; p.write_text(json.dumps({'schema': 99}))
        with self.assertRaisesRegex(ValueError, 'Unknown'):
            load_config(p)
        self.assertEqual(json.loads(p.read_text())['schema'], 99)


if __name__ == '__main__':
    unittest.main()
