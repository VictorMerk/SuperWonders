"""Edition launch boundaries, using synthetic executables without starting a game."""
import json
import contextlib
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from wonder_library import (default_config, launch_spec, load_config, save_config,
                            installation_health, diagnostic_report, exit_message, main, REQUIRED_FILES, GAME_LIBRARY, active_user_root)


class LibraryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.executable = self.root / 'Wonder & flower [test].exe'
        self.executable.write_bytes(b'synthetic executable placeholder')
        self.data = self.root / 'original data'; self.data.mkdir()
        for name in REQUIRED_FILES:
            target = self.data / name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(b'prepared')
        (self.data/'extract/romfs').mkdir(parents=True)
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
                     'NX_FRAMETRACE': 'wrong.csv', 'NX_NATIVE_LAN':'1', 'NX_NATIVE_NET_TRACE':'1',
                     'NX_STEAM_DEV':'1', 'NX_STEAM_API_LIBRARY':'wrong.dll', 'SteamAppId':'480',
                     'SteamGameId':'480', 'NX_CONTROLS_TOUR_SECTION':'1', 'NX_PIPELINE_MISS_TRACE':'wrong.csv'}
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

    def test_missing_assets_block_play_but_keep_setup_available(self):
        (self.data/'game'/GAME_LIBRARY).unlink()
        with self.assertRaisesRegex(ValueError, GAME_LIBRARY):
            launch_spec(self.config, 'original')
        self.assertEqual(launch_spec(self.config, 'original', configure=True)[0][-1], '--config')

    def test_empty_prepared_file_is_not_ready(self):
        (self.data/'analysis/main.bin').write_bytes(b'')
        self.assertTrue(any('main.bin' in issue for issue in installation_health(self.config['editions']['original'])))

    def test_nested_data_roots_are_rejected_in_both_directions(self):
        for other in (self.data/'nested', self.root):
            self.config['editions']['switch2']['data_root'] = str(other)
            with self.assertRaisesRegex(ValueError, 'separate game-data'):
                launch_spec(self.config, 'original')

    def test_portable_relative_paths_are_resolved_against_config(self):
        p = self.root/'portable.json'
        config = default_config()
        config['editions']['original'] = {'executable':self.executable.name, 'data_root':self.data.name}
        p.write_text(json.dumps(config), encoding='utf-8-sig')
        self.assertEqual(load_config(p),self.config)

    def test_invalid_configuration_shapes_and_nul_paths_are_rejected(self):
        p = self.root/'invalid.json'
        for value in ([], {'schema':1,'editions':[]}, {'schema':1,'editions':{'original':None}},
                      {'schema':1,'editions':{'original':{'executable':'bad\0.exe'}}}):
            p.write_text(json.dumps(value))
            with self.assertRaises(ValueError):load_config(p)

    def test_previous_config_is_backed_up_and_no_temporary_files_remain(self):
        p = self.root/'library.json';save_config(p,self.config)
        old = p.read_bytes();save_config(p,default_config())
        self.assertEqual(p.with_suffix('.json.bak').read_bytes(),old)
        self.assertEqual(list(self.root.glob('*.tmp')),[])

    def test_failed_replace_keeps_original_and_cleans_temporary(self):
        p = self.root/'library.json';save_config(p,self.config);old=p.read_bytes()
        with patch.object(Path,'replace',side_effect=OSError('blocked')):
            with self.assertRaises(OSError):save_config(p,default_config())
        self.assertEqual(p.read_bytes(),old)
        self.assertEqual(list(self.root.glob('*.tmp')),[])

    def test_redacted_report_contains_no_private_paths_or_environment(self):
        with patch.dict(os.environ,{'NX_LAN_CODE':'secret-example'}):
            report=json.dumps(diagnostic_report(self.config))
        self.assertNotIn(str(self.root),report)
        self.assertNotIn(self.executable.name,report)
        self.assertNotIn('secret-example',report)
        self.assertIn('Preparation checks',report)

    def test_cli_check_and_export_work_without_gui(self):
        config = self.root/'library.json';save_config(config,self.config)
        out=self.root/'report.json'
        with patch.object(sys,'argv',['wonder_library','--config',str(config),'--check','--diagnostics',str(out)]):
            with contextlib.redirect_stdout(io.StringIO()) as captured:
                self.assertEqual(main(),1)  # second edition is not configured
        self.assertEqual(json.loads(captured.getvalue()),json.loads(out.read_text()))

    def test_save_shortcut_follows_only_valid_active_profiles(self):
        self.assertEqual(active_user_root(self.data),self.data/'user')
        profile=self.data/'profiles/p-123-4';profile.mkdir(parents=True)
        (profile/'profile-name.txt').write_text('Player 2')
        active=self.data/'active-profile.txt';active.write_text('p-123-4')
        self.assertEqual(active_user_root(self.data),profile/'user')
        active.write_text('../../other')
        self.assertEqual(active_user_root(self.data),self.data/'user')
        active.write_text('p-123-4');(profile/'profile-name.txt').write_text('')
        self.assertEqual(active_user_root(self.data),self.data/'user')

    def test_exit_code_reports_native_crashes_and_normal_closure(self):
        self.assertEqual(exit_message(0),'Closed normally')
        self.assertIn('0xC0000005',exit_message(-1073741819))
        self.assertIn('0x00000002',exit_message(2))


if __name__ == '__main__':
    unittest.main()
