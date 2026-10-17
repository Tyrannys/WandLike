import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
os.environ["APPDATA"] = tempfile.mkdtemp(prefix="wl-test-appdata-")

from wandlike.core import authoring
from wandlike.core.plugin import GAMES_DIR, load_game, parse_game


def good(**over):
    p = {"id": "demo", "name": "Demo", "exe": "demo.exe", "window_title": "Demo", "launch_args": "", "console": {},
         "match": {"steam_appid": 1}, "version": 1,
         "cheats": [{"id": "gold", "label": "Gold", "kind": "number", "info": "x", "set": "setgold {value}"},
                    {"id": "god", "label": "God", "kind": "toggle", "info": "x", "on": ["god"], "off": ["god"]}]}
    p.update(over)
    return p


def check(plugin):
    return authoring.validate_text(json.dumps(plugin))


class Validate(unittest.TestCase):
    def test_good_plugin_has_no_errors(self):
        errors, warns = check(good())
        self.assertEqual((errors, warns), ([], []))

    def test_every_bundled_plugin_validates(self):
        for p in GAMES_DIR.glob("*.json"):
            if not p.name.startswith("_"):
                errors, _ = authoring.validate_file(p)
                self.assertEqual(errors, [], p.name)

    def test_typo_gets_a_suggestion(self):
        bad = good()
        bad["cheats"][0]["sett"] = "x"
        errors, _ = check(bad)
        self.assertTrue(any('"sett"' in e and 'did you mean "set"' in e for e in errors), errors)

    def test_template_without_value_or_with_stray_brace(self):
        for tpl in ("setgold 5", "set {val}", "effect x = { value = {value} }"):
            bad = good()
            bad["cheats"][0]["set"] = tpl
            self.assertTrue(check(bad)[0], tpl)

    def test_doubled_braces_in_on_lines_warn(self):
        bad = good()
        bad["cheats"][1]["on"] = ["effect x = {{ y = 1 }}"]
        self.assertTrue(any("doubled braces" in w for w in check(bad)[1]))

    def test_missing_required_and_bad_json(self):
        self.assertTrue(authoring.validate_text("{")[0][0].startswith("not valid JSON"))
        bad = good()
        del bad["exe"]
        self.assertTrue(any('missing required key "exe"' in e for e in check(bad)[0]))

    def test_unknown_memory_symbol(self):
        bad = good()
        bad["cheats"][1]["memory"] = {"symbol": "nope", "on": 1}
        self.assertTrue(any("not defined under hook.symbols" in e for e in check(bad)[0]))

    def test_default_outside_range(self):
        bad = good()
        bad["cheats"][0].update(min=0, max=10, default=50)
        self.assertTrue(any("outside min/max" in e for e in check(bad)[0]))


class PreviewAndScaffold(unittest.TestCase):
    def test_preview_lists_commands(self):
        text = "\n".join(authoring.preview(parse_game(good())))
        self.assertIn("setgold", text)
        self.assertIn("on  -> ['god']", text)

    def test_scaffold_from_library_entry_is_valid(self):
        tmp = Path(tempfile.mkdtemp())
        (tmp / "game.exe").write_bytes(b"x" * 100)
        (tmp / "unins000.exe").write_bytes(b"x" * 9999)       # installers must not be picked
        entry = {"store": "steam", "id": "236850", "name": "Europa: Universalis IV!", "install_dir": str(tmp)}
        plugin = authoring.scaffold(entry)
        self.assertEqual(plugin["id"], "europa-universalis-iv")
        self.assertEqual((plugin["exe"], plugin["match"], plugin["steam_appid"]), ("game.exe", {"steam_appid": 236850}, 236850))
        self.assertEqual(check(plugin)[0], [])

    def test_scaffold_epic_and_gog_use_their_ids(self):
        e = authoring.scaffold({"store": "epic", "id": "Salt", "name": "X", "install_dir": ".", "exe": "bin/x.exe"})
        self.assertEqual((e["match"], e["exe"]), ({"epic": "Salt"}, "x.exe"))
        g = authoring.scaffold({"store": "gog", "id": "123", "name": "Y", "install_dir": ".", "exe": "y.exe"})
        self.assertEqual(g["match"], {"gog": "123"})


if __name__ == "__main__":
    unittest.main()
