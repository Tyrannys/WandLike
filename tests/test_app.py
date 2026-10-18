import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
os.environ["APPDATA"] = tempfile.mkdtemp(prefix="wl-test-appdata-")  # isolate settings

from wandlike.core.hotkeys import parse_combo


class HotkeyParse(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(parse_combo("Ctrl+Shift+F5"), (2 | 4, 0x74))
        self.assertEqual(parse_combo("Alt+A"), (1, ord("A")))
        self.assertEqual(parse_combo("Num5"), (0, 0x65))
        self.assertEqual(parse_combo("Ctrl+`"), (2, 0xC0))

    def test_invalid(self):
        self.assertIsNone(parse_combo("Ctrl+Shift"))
        self.assertIsNone(parse_combo("Ctrl+Banana"))
        self.assertIsNone(parse_combo(""))


class ApiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from wandlike.app.api import Api
        cls.api = Api(dry_run=True, background=False)

    def setUp(self):
        self.api.unhook("ck3")

    def hooked(self, console=True):
        self.assertTrue(self.api.hook("ck3")["ok"])
        if console:
            self.assertTrue(self.api.set_toggle("ck3", "enable_console", True)["ok"])

    def test_bootstrap_and_game(self):
        b = self.api.bootstrap()
        self.assertEqual(b["games"][0]["id"], "ck3")
        g = self.api.game("ck3")
        self.assertEqual(len(g["cheats"]), 21)
        self.assertEqual(g["cheats"][0]["id"], "enable_console")
        self.assertTrue(g["needs_console"])
        self.assertEqual({c["id"]: c["source"] for c in g["cheats"]}["freeze_time"], "memory")
        self.assertEqual({c["id"]: c["source"] for c in g["cheats"]}["money"], "console")
        self.assertTrue(any(c["id"] == "treasury" for c in g["cheats"]))

    def test_every_cheat_needs_the_game_hooked(self):
        for call in (lambda: self.api.set_number("ck3", "dread", 50, "set"),
                     lambda: self.api.set_toggle("ck3", "instabuild", True),
                     lambda: self.api.set_toggle("ck3", "freeze_time", True),
                     lambda: self.api.set_toggle("ck3", "enable_console", True)):
            r = call()
            self.assertFalse(r["ok"])
            self.assertIn("hook the game first", r["error"])

    def test_console_cheats_need_enable_console_but_memory_cheats_do_not(self):
        self.hooked(console=False)
        r = self.api.set_number("ck3", "dread", 50, "set")
        self.assertEqual((r["ok"], r["error"]), (False, "turn on Enable Console first"))
        self.assertFalse(self.api.set_toggle("ck3", "instabuild", True)["ok"])
        self.assertTrue(self.api.set_toggle("ck3", "freeze_time", True)["ok"])      # memory cheat: hook is enough
        self.assertEqual(self.api.state()["console_ready"], {"ck3": False})

    def test_number_clamped_once_hooked_with_console(self):
        self.hooked()
        self.assertEqual(self.api.state()["console_ready"], {"ck3": True})
        r = self.api.set_number("ck3", "dread", 500, "set")
        self.assertEqual(r, {"value": 100, "ok": True, "error": ""})

    def test_unhook_turns_everything_off_again(self):
        self.hooked()
        self.api.set_toggle("ck3", "freeze_time", True)
        self.api.unhook("ck3")
        self.assertFalse(self.api.state()["hooked"]["ck3"])
        self.assertEqual({k: v for k, v in self.api.state()["toggles"]["ck3"].items() if v}, {})
        self.assertFalse(self.api.set_number("ck3", "dread", 50, "set")["ok"])

    def test_pin_and_preset_roundtrip(self):
        self.hooked()
        self.api.pin("ck3", "money", True)
        self.assertTrue(next(c for c in self.api.game("ck3")["cheats"] if c["id"] == "money")["pinned"])
        self.api.set_toggle("ck3", "instabuild", True)
        self.api.save_preset("ck3")
        self.assertTrue(self.api.game("ck3")["has_preset"])
        self.api.apply_preset("ck3")

    def test_preset_enables_console_before_console_cheats(self):
        self.hooked()
        self.api.set_toggle("ck3", "instabuild", True)
        self.api.save_preset("ck3")
        self.api.unhook("ck3")
        self.assertTrue(self.api.hook("ck3")["ok"])          # fresh hook: console is off again
        self.api.apply_preset("ck3")                          # must flip Enable Console first, or instabuild is refused
        self.assertTrue(self.api.state()["toggles"]["ck3"]["instabuild"])

    def test_bad_hotkey_rejected(self):
        self.assertTrue(self.api.set_hotkey("ck3", "dread", "Ctrl+Nope"))


class PluginUpdateFlow(unittest.TestCase):
    """A game's .json edited on GitHub (no version bump anywhere) reaches the running launcher."""

    def test_updated_json_reaches_the_loaded_game_and_nothing_happens_when_unchanged(self):
        import json
        from wandlike.app.api import Api
        from wandlike.core.catalog import Catalog, sha256
        from wandlike.core.plugin import GAMES_DIR

        raw = json.loads((GAMES_DIR / "ck3.json").read_text(encoding="utf-8"))
        raw["notes"] = "EDITED ON GITHUB, same version number"
        body = json.dumps(raw).encode()
        index = {"schema": 1, "games": [{"id": "ck3", "name": raw["name"], "version": raw["version"], "file": "ck3.json",
                                          "sha256": sha256(body), "match": raw["match"], "cheats": 20,
                                          "min_app": raw["min_app"]}]}

        def fetch(url, etag=None, timeout=10):
            name = url.rsplit("/", 1)[-1]
            return (200, "e1", json.dumps(index).encode()) if name == "_index.json" else (200, None, body)

        api = Api(dry_run=True, background=False)
        api._catalog = Catalog(repo="me/wl", fetch=fetch)
        self.addCleanup(lambda: [f.unlink() for f in api._catalog.games_dir.glob("*.json")])   # don't leak into other tests
        api._detected = [{"store": "steam", "id": "1158310", "name": "CK3", "install_dir": "x"}]
        self.assertNotEqual(api.game("ck3")["notes"], raw["notes"])
        rev = api._rev

        api._sync_catalog()
        self.assertEqual(api.game("ck3")["notes"], raw["notes"])             # new content is live
        self.assertEqual(api._games["ck3"].source, "catalog")
        self.assertGreater(api._rev, rev)                                    # the UI is told to refresh
        self.assertTrue(any("Updated support" in n["text"] for n in api.state()["notices"]))

        rev = api._rev
        api._sync_catalog()                                                   # nothing changed upstream
        self.assertEqual(api._rev, rev)
        self.assertEqual(len(api.state()["notices"]), 1)


if __name__ == "__main__":
    unittest.main()
