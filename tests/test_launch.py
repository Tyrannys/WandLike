import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
os.environ["APPDATA"] = tempfile.mkdtemp(prefix="wl-test-appdata-")

from wandlike.core import engine as engine_mod
from wandlike.core.engine import Engine
from wandlike.core.plugin import parse_game


def make_game(args="-debug_mode"):
    return parse_game({"id": "demo", "name": "Demo", "exe": "demo.exe", "window_title": "Demo", "launch_args": args,
                       "console": {}, "match": {"steam_appid": 77},
                       "cheats": [{"id": "x", "label": "X", "kind": "button", "run": ["x"]}]})


class Launch(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="wl-launch-"))
        (self.root / "binaries").mkdir()
        (self.root / "binaries" / "demo.exe").write_bytes(b"MZ")
        self.lib = {"store": "steam", "id": "77", "name": "Demo", "install_dir": str(self.root)}

    def launch(self, game, lib=None):
        eng = Engine(game, lambda m: None)
        with mock.patch.object(engine_mod.subprocess, "Popen") as popen, mock.patch.object(engine_mod.os, "startfile", create=True) as sf:
            eng.launch(lib or self.lib)
        return popen, sf

    def test_find_exe_in_binaries_folder(self):
        eng = Engine(make_game(), lambda m: None)
        self.assertEqual(eng._find_exe(str(self.root)), self.root / "binaries" / "demo.exe")
        self.assertIsNone(eng._find_exe(str(self.root / "nothing")))

    def test_args_with_steam_appid_file_start_exe_directly(self):
        (self.root / "binaries" / "steam_appid.txt").write_text("77")
        popen, sf = self.launch(make_game())
        args, kw = popen.call_args
        self.assertEqual(args[0], [str(self.root / "binaries" / "demo.exe"), "-debug_mode"])
        self.assertEqual(kw["cwd"], str(self.root / "binaries"))
        sf.assert_not_called()                      # no steam:// URL, so no Steam confirmation prompt

    def test_args_without_appid_file_fall_back_to_steam_url(self):
        popen, sf = self.launch(make_game())
        popen.assert_not_called()
        sf.assert_called_once_with("steam://run/77//-debug_mode/")

    def test_no_args_uses_plain_steam_launch(self):
        popen, sf = self.launch(make_game(args=""))
        sf.assert_called_once_with("steam://rungameid/77")

    def test_epic_launch_uses_launcher_url(self):
        popen, sf = self.launch(make_game(args=""), {"store": "epic", "id": "Salt", "name": "D", "install_dir": str(self.root)})
        self.assertIn("com.epicgames.launcher://apps/Salt", sf.call_args[0][0])


if __name__ == "__main__":
    unittest.main()
