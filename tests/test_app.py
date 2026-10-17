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
        cls.api = Api(dry_run=True)

    def test_bootstrap_and_game(self):
        b = self.api.bootstrap()
        self.assertEqual(b["games"][0]["id"], "ck3")
        g = self.api.game("ck3")
        self.assertEqual(len(g["cheats"]), 21)
        self.assertTrue(any(c["id"] == "treasury" for c in g["cheats"]))

    def test_number_clamped_and_logged(self):
        v = self.api.set_number("ck3", "dread", 500, "set")
        self.assertEqual(v, 100)

    def test_unsupported_toggle_refused(self):
        self.assertFalse(self.api.set_toggle("ck3", "freeze_time", True))

    def test_pin_and_preset_roundtrip(self):
        self.api.pin("ck3", "money", True)
        self.assertTrue(next(c for c in self.api.game("ck3")["cheats"] if c["id"] == "money")["pinned"])
        self.api.set_toggle("ck3", "instabuild", True)
        self.api.save_preset("ck3")
        self.assertTrue(self.api.game("ck3")["has_preset"])
        self.api.apply_preset("ck3")

    def test_bad_hotkey_rejected(self):
        self.assertTrue(self.api.set_hotkey("ck3", "dread", "Ctrl+Nope"))


if __name__ == "__main__":
    unittest.main()
