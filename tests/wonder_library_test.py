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

    def test_native_management_uses_paired_launcher_and_keeps_edition_root(self):
        launcher = self.root / 'wonder-launcher.exe'
        launcher.write_bytes(b'synthetic launcher')
        argv, cwd, env = launch_spec(self.config, 'original', configure=True)
        expected = launcher if os.name == 'nt' else self.executable
        self.assertEqual(argv, [str(expected.resolve()), '--config'])
        self.assertEqual(cwd, str(self.root.resolve()))
        self.assertEqual(env['NX_DATA_ROOT'], str(self.data.resolve()))

    def test_native_management_falls_back_to_runtime_config_entry(self):
        argv, _, _ = launch_spec(self.config, 'original', configure=True)
        self.assertEqual(argv, [str(self.executable.resolve()), '--config'])

    def test_debug_and_lan_session_overrides_do_not_leak_into_normal_launch(self):
        transient = {'NX_HEADLESS': '1', 'NX_INPUT_SCRIPT': '0:A:9', 'NX_LAN_LOBBY_AUTOSTART': 'host',
                     'NX_LAN_CODE': 'test', 'NX_PROFILE_MANAGER_TOUR': '1', 'NX_FRAMEGEN_PROBE': '1',
                     'NX_FRAMETRACE': 'wrong.csv'}
        with patch.dict(os.environ, transient | {'NX_FRAMEGEN': '1', 'NX_GPU': 'chosen'}):
            _, _, env = launch_spec(self.config, 'original')
        for name in transient:
            self.assertNotIn(name, env)
        self.assertEqual(env['NX_FRAMEGEN'], '1')
        self.assertEqual(env['NX_GPU'], 'chosen')

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
