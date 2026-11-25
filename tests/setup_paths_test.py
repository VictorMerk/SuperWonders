"""Literal filesystem roots in the user's setup path, using synthetic files only."""
from pathlib import Path
import contextlib
import io
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT))
import gamebuild
import setup_game
import wonder_menu_mod
from tools.maxwell import extract_romfs_shaders as shaders


class SetupPaths(unittest.TestCase):
    @unittest.skipUnless(sys.platform == "win32", "Windows junction API")
    def test_junction_treats_shell_metacharacters_and_environment_names_literally(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source&echo %TEMP% [files]"
            target = root / "target&echo %TEMP% [files]"
            source.mkdir()
            (source / "fixture.txt").write_text("literal", encoding="utf-8")
            with mock.patch.object(setup_game.subprocess, "run", side_effect=AssertionError("must not invoke a shell")):
                setup_game.make_link(str(source), str(target))
            try:
                self.assertEqual((target / "fixture.txt").read_text(encoding="utf-8"), "literal")
            finally:
                setup_game.remove_link(str(target))
            self.assertTrue((source / "fixture.txt").is_file())

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="wonder-path-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "Wonder [BASE] ä files"
        self.exefs = self.root / "ExeFS"
        self.romfs = self.root / "RomFS"

    def file(self, relative, data=b"fixture"):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_check_inputs_accepts_literal_brackets_and_still_rejects_missing_shaders(self):
        self.file("ExeFS/main", b"NSO0fixture")
        self.file("ExeFS/sdk", b"NSO0fixture")
        shader = self.file("RomFS/Shader/example.bfsha.zs")
        setup = setup_game.Setup(SimpleNamespace(exefs=str(self.exefs), romfs=str(self.romfs),
            data_root=str(self.root / "data"), romfs_mode="copy", jobs=1))
        setup.check_inputs()
        shader.unlink()
        with self.assertRaisesRegex(SystemExit, r"no Shader/\*\.zs"):
            setup.check_inputs()

    def test_every_archive_group_and_catalog_use_literal_romfs(self):
        expected = {self.file("RomFS/"+name) for name in (
            "Shader/example.zs", "Effect/example.esetb.byml.zs", "Layout/Test.Nin_NX_NVN.blarc.zs",
            "Lib/agl/example.sarc.zs", "Lib/gsys/example.sarc.zs")}
        visited = []
        def read(path): visited.append(Path(path)); return b"synthetic archive"
        output = self.root / "output"
        output.mkdir()
        # Exercise discovery in both real extraction loops without Nintendo archives.
        with mock.patch.object(shaders.containers, "read_file", side_effect=read), \
             mock.patch.object(shaders.containers, "open_blob", return_value=object()), \
             mock.patch.object(shaders.containers, "extract", return_value=1), \
             mock.patch.object(shaders.containers, "all_stages", side_effect=lambda _: iter([object()])), \
             mock.patch.object(shaders.containers, "walk", return_value=[]), contextlib.redirect_stdout(io.StringIO()):
            shaders.extract_romfs(str(self.romfs), str(output))
        self.assertEqual(set(visited), expected)
        self.assertEqual(len(visited), 10)  # extraction + program catalog
        self.assertTrue((output / "shader_programs.bin").read_bytes().startswith(shaders.CATALOG_MAGIC))

    def test_compilation_discovers_generated_units_inside_bracket_path(self):
        sources = {self.file("generated/main/a.cpp"), self.file("generated/sdk/b.cpp"), self.file("gamelib_entry.cpp")}
        calls = []
        def compiler(command, **kwargs):
            calls.append(Path(command[command.index("-c")+1]))
            Path(command[-1]).write_bytes(b"synthetic object")
            return SimpleNamespace(returncode=0, stderr="")
        kit = SimpleNamespace(include=str(self.root), entry=str(self.root / "gamelib_entry.cpp"), cxx="not-executed", cxxflags=[])
        with mock.patch.object(gamebuild.subprocess, "run", side_effect=compiler), contextlib.redirect_stdout(io.StringIO()):
            gamebuild.compile_all(kit, str(self.root / "generated"), str(self.root / "objects"), 1, 0, 1)
        self.assertEqual(set(calls), sources)

    def test_kit_dependency_inventory_under_literal_root(self):
        dependency = self.file("recomp/emit.py")
        kit = gamebuild.Kit()
        kit.recomp = str(self.root / "recomp")
        self.assertIn(str(dependency), kit.files())

    def test_menu_size_table_uses_literal_path_and_checks_ambiguity(self):
        table = self.file("RomFS/System/Resource/ResourceSizeTable.Product.100.rsizetable.zs")
        self.assertEqual(Path(wonder_menu_mod.resource_size_table(str(self.romfs))), table)
        self.file("RomFS/System/Resource/ResourceSizeTable.Product.101.rsizetable.zs")
        with self.assertRaisesRegex(AssertionError, "one resource size table expected"):
            wonder_menu_mod.resource_size_table(str(self.romfs))

    def test_recompiler_reports_real_phases_and_function_counts(self):
        events = []
        worker = SimpleNamespace(stdout=iter([]), wait=lambda: 0)
        def child(*args, **kwargs):
            worker.stdout = iter(["phase read\n", "phase analyze\n", "phase emit\n", "  emitted 5/10 functions\n"])
            return worker
        kit = SimpleNamespace(root=str(self.root))
        with mock.patch.object(gamebuild.subprocess, "Popen", side_effect=child), \
             mock.patch.object(gamebuild, "progress", side_effect=lambda f, text: events.append((f, text))):
            gamebuild.recompile(kit, str(self.exefs), str(self.root / "gen"), .1, .6)
        labels = [text for _, text in events]
        self.assertIn("Reading main executable", labels)
        self.assertIn("Analyzing main functions", labels)
        self.assertIn("Generating main code", labels)
        self.assertIn("Generating main: 5/10 functions", labels)
        self.assertIn("Generating sdk: 5/10 functions", labels)
        self.assertEqual([f for f, _ in events], sorted(f for f, _ in events))
        self.assertAlmostEqual(events[-1][0], .7)

    def test_overall_progress_does_not_finish_before_layout_preparation(self):
        events = []
        setup = setup_game.Setup(SimpleNamespace(exefs=str(self.exefs), romfs=str(self.romfs),
            data_root=str(self.root / "data"), romfs_mode="copy", jobs=1))
        def run(args, at, share, what):
            setup_game.say(at, what)
            setup_game.say(at + share, what)
        with mock.patch.object(setup, "check_inputs"), mock.patch.object(setup, "needed", return_value=[]), \
             mock.patch.object(setup, "run", side_effect=run), \
             mock.patch.object(setup_game, "say", side_effect=lambda f, text: events.append((f, text))):
            self.assertEqual(setup.setup(), 0)
        self.assertEqual([f for f, _ in events], sorted(f for f, _ in events))
        layout = next(i for i, (_, text) in enumerate(events) if text == "Preparing ultrawide layouts")
        self.assertTrue(all(f < 1 for f, _ in events[:layout]))
        self.assertEqual(events[-1][0], 1)

if __name__ == "__main__": unittest.main()
