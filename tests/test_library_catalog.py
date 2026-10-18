import hashlib
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
os.environ["APPDATA"] = tempfile.mkdtemp(prefix="wl-test-appdata-")

from wandlike.core import library
from wandlike.core.catalog import Catalog, sha256
from wandlike.core.plugin import load_all, parse_game

ACF = '"AppState"\n{\n\t"appid"\t\t"%(id)s"\n\t"name"\t\t"%(name)s"\n\t"installdir"\t\t"%(dir)s"\n}\n'


def make_steam(root: Path, games: dict[str, str]) -> Path:
    apps = root / "steamapps"
    (apps / "common").mkdir(parents=True)
    (apps / "libraryfolders.vdf").write_text(f'"libraryfolders" {{ "0" {{ "path" "{str(root).replace(chr(92), chr(92)*2)}" }} }}')
    for appid, name in games.items():
        (apps / "common" / name).mkdir()
        (apps / f"appmanifest_{appid}.acf").write_text(ACF % dict(id=appid, name=name, dir=name))
    return apps


def make_epic(d: Path, items: dict[str, str], base: Path) -> None:
    d.mkdir(parents=True)
    for app, name in items.items():
        loc = base / name
        loc.mkdir(parents=True, exist_ok=True)
        (d / f"{app}.item").write_text(json.dumps({"AppName": app, "DisplayName": name, "InstallLocation": str(loc),
                                                   "LaunchExecutable": "g.exe"}))


class LibraryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="wl-lib-"))
        self.steam = self.tmp / "Steam"
        self.apps = make_steam(self.steam, {"1158310": "Crusader Kings III", "10": "Counter-Strike"})
        make_epic(self.tmp / "epic", {"Salt": "Fortnite"}, self.tmp / "epicgames")
        self.lib = library.Library(self.tmp / "cache.json", steam_root=lambda: self.steam,
                                   epic_dir=self.tmp / "epic", scan_gog=False)

    def test_finds_games_in_every_store(self):
        games = self.lib.scan()
        self.assertEqual({(g["store"], g["id"]) for g in games},
                         {("steam", "1158310"), ("steam", "10"), ("epic", "Salt")})
        self.assertEqual(library.find(games, {"steam_appid": 1158310})["name"], "Crusader Kings III")
        self.assertEqual(library.find(games, {"epic": "Salt"})["store"], "epic")
        self.assertIsNone(library.find(games, {"steam_appid": 999}))

    def test_second_scan_reads_nothing(self):
        self.lib.scan()
        self.assertGreater(self.lib.stats["files_parsed"], 0)
        self.lib.scan()
        self.assertEqual(self.lib.stats["files_parsed"], 0)
        self.assertEqual(self.lib.stats["dirs_listed"], 0)
        self.assertGreater(self.lib.stats["files_cached"], 0)

    def test_cache_survives_restart(self):
        self.lib.scan()
        again = library.Library(self.tmp / "cache.json", steam_root=lambda: self.steam, epic_dir=self.tmp / "epic", scan_gog=False)
        again.scan()
        self.assertEqual(again.stats["files_parsed"], 0)

    def test_new_install_and_uninstall_detected(self):
        self.lib.scan()
        (self.apps / "common" / "Stellaris").mkdir()
        (self.apps / "appmanifest_281990.acf").write_text(ACF % dict(id="281990", name="Stellaris", dir="Stellaris"))
        os.utime(self.apps, None)  # make sure the directory mtime moves even on coarse clocks
        names = {g["name"] for g in self.lib.scan()}
        self.assertIn("Stellaris", names)
        self.assertEqual(self.lib.stats["files_parsed"], 1)   # only the new manifest was read
        (self.apps / "appmanifest_281990.acf").unlink()
        os.utime(self.apps, None)
        self.assertNotIn("Stellaris", {g["name"] for g in self.lib.scan()})

    def test_changed_manifest_is_reparsed(self):
        self.lib.scan()
        p = self.apps / "appmanifest_10.acf"
        p.write_text(ACF % dict(id="10", name="CS Renamed", dir="Counter-Strike") + "\n")
        self.assertIn("CS Renamed", {g["name"] for g in self.lib.scan()})
        self.assertEqual(self.lib.stats["files_parsed"], 1)

    def test_force_rescan_rereads_everything(self):
        self.lib.scan()
        self.lib.scan(force=True)
        self.assertGreater(self.lib.stats["files_parsed"], 0)

    def test_missing_install_folder_is_ignored(self):
        import shutil
        shutil.rmtree(self.apps / "common" / "Counter-Strike")
        self.assertNotIn("10", {g["id"] for g in self.lib.scan()})


# ------------------------------------------------------------------ catalog
def plugin(gid="newgame", version=1, **over):
    p = {"id": gid, "name": "New Game", "exe": "ng.exe", "window_title": "New Game", "launch_args": "", "console": {},
         "version": version, "match": {"steam_appid": 77},
         "cheats": [{"id": "gold", "label": "Gold", "kind": "number", "set": "gold {value}"}]}
    p.update(over)
    return json.dumps(p, indent=1).encode()


class FakeRepo:
    """Stands in for raw.githubusercontent.com."""
    def __init__(self):
        self.files: dict[str, bytes] = {}
        self.etag = "v1"
        self.calls: list[tuple[str, str | None]] = []
        self.index_status = 200

    def publish(self, body: bytes, gid="newgame", version=1, match=None, **entry):
        name = f"{gid}.json"
        self.files[name] = body
        idx = [e for e in json.loads(self.files.get("_index.json", b'{"games": []}'))["games"] if e["id"] != gid]
        idx.append({"id": gid, "name": "New Game", "version": version, "file": name, "sha256": sha256(body),
                    "match": match or {"steam_appid": 77}, "cheats": 1, **entry})
        self.files["_index.json"] = json.dumps({"schema": 1, "games": idx}).encode()
        self.etag = f"e{len(self.calls)}x{version}"

    def __call__(self, url, etag=None, timeout=10):
        name = url.rsplit("/", 1)[-1]
        self.calls.append((name, etag))
        if name == "_index.json":
            if self.index_status != 200:
                return self.index_status, None, b""
            return (304, None, b"") if etag == self.etag else (200, self.etag, self.files[name])
        return (200, None, self.files[name]) if name in self.files else (404, None, b"")


DETECTED = [{"store": "steam", "id": "77", "name": "New Game", "install_dir": "x"}]


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.repo = FakeRepo()
        self.dir = Path(tempfile.mkdtemp(prefix="wl-cat-"))
        self.cat = Catalog(repo="me/wl", directory=self.dir, fetch=self.repo, app_version="1.0.0")

    def downloads(self):
        return [c for c in self.repo.calls if c[0] != "_index.json"]

    def test_downloads_plugin_only_when_game_is_detected(self):
        self.repo.publish(plugin())
        self.assertEqual(self.cat.sync([]), [])                 # game not on this PC
        self.assertEqual(self.downloads(), [])
        msgs = self.cat.sync(DETECTED)
        self.assertEqual(msgs, ["Added support for New Game"])
        self.assertTrue((self.dir / "games" / "newgame.json").exists())

    def test_downloaded_plugin_is_loaded(self):
        self.repo.publish(plugin())
        self.cat.sync(DETECTED)
        games = load_all(catalog_dir=self.dir / "games", include_user=False)
        self.assertEqual(games["newgame"].source, "catalog")

    def test_no_redownload_when_up_to_date(self):
        self.repo.publish(plugin())
        self.cat.sync(DETECTED)
        n = len(self.downloads())
        self.cat.sync(DETECTED)
        self.assertEqual(len(self.downloads()), n)

    def test_index_uses_etag(self):
        self.repo.publish(plugin())
        self.cat.sync(DETECTED)
        self.cat.sync(DETECTED)
        self.assertEqual(self.repo.calls[-1], ("_index.json", self.repo.etag))   # sent the stored ETag
        self.assertIn("up to date", self.cat.status)

    def test_changed_content_is_updated_without_any_version_bump(self):
        self.repo.publish(plugin(version=1))
        self.cat.sync(DETECTED)
        self.repo.publish(plugin(version=1, notes="edited on GitHub"), version=1)      # same version number, new content
        msgs = self.cat.sync(DETECTED)
        self.assertEqual(msgs, ["Updated support for New Game"])
        self.assertIn("edited on GitHub", (self.dir / "games" / "newgame.json").read_text(encoding="utf-8"))

    def test_nothing_is_downloaded_when_the_bundled_copy_is_already_current(self):
        body = plugin()
        bundled = Path(tempfile.mkdtemp(prefix="wl-bundled-"))
        (bundled / "newgame.json").write_bytes(body)
        cat = Catalog(repo="me/wl", directory=Path(tempfile.mkdtemp(prefix="wl-cat2-")), fetch=self.repo,
                      app_version="1.0.0", bundled_dir=bundled)
        self.repo.publish(body)
        self.assertEqual(cat.sync(DETECTED), [])
        self.assertEqual(self.downloads(), [])
        self.repo.publish(plugin(notes="newer than the bundled one"))
        self.assertEqual(cat.sync(DETECTED), ["Updated support for New Game"])

    def test_line_endings_do_not_cause_redownloads(self):
        body = plugin()
        self.repo.publish(body)
        self.cat.sync(DETECTED)
        (self.dir / "games" / "newgame.json").write_bytes(body.replace(b"\n", b"\r\n"))   # Windows-style copy
        n = len(self.downloads())
        self.cat.sync(DETECTED)
        self.assertEqual(len(self.downloads()), n)

    def test_new_app_version_drops_downloaded_copies_and_repulls(self):
        self.repo.publish(plugin())
        self.cat.sync(DETECTED)
        self.assertTrue((self.dir / "games" / "newgame.json").exists())
        newer = Catalog(repo="me/wl", directory=self.dir, fetch=self.repo, app_version="1.1.0")    # app was updated
        self.assertFalse((self.dir / "games" / "newgame.json").exists())
        self.assertEqual(newer.sync(DETECTED), ["Added support for New Game"])

    def test_hash_mismatch_is_rejected(self):
        self.repo.publish(plugin())
        self.repo.files["newgame.json"] = plugin(notes="tampered")
        msgs = self.cat.sync(DETECTED)
        self.assertIn("SHA-256", msgs[0])
        self.assertFalse((self.dir / "games" / "newgame.json").exists())

    def test_invalid_plugin_is_rejected_even_with_right_hash(self):
        bad = plugin(evil_field=1)
        self.repo.publish(bad)
        msgs = self.cat.sync(DETECTED)
        self.assertIn("not a valid plugin", msgs[0])
        self.assertFalse((self.dir / "games" / "newgame.json").exists())

    def test_path_traversal_file_name_ignored(self):
        self.repo.publish(plugin())
        idx = json.loads(self.repo.files["_index.json"])
        idx["games"][0]["file"] = "../../evil.json"
        self.repo.files["_index.json"] = json.dumps(idx).encode()
        self.assertEqual(self.cat.sync(DETECTED), [])

    def test_private_repo_reports_status_not_crash(self):
        self.repo.index_status = 404
        self.assertEqual(self.cat.sync(DETECTED), [])
        self.assertIn("unavailable", self.cat.status)

    def test_plugin_needing_newer_app_is_skipped(self):
        self.repo.publish(plugin(), min_app="9.0.0")
        msgs = self.cat.sync(DETECTED)
        self.assertIn("needs a newer WandLike", msgs[0])
        self.assertEqual(self.downloads(), [])

    def test_explore_entries(self):
        self.repo.publish(plugin())
        self.cat.fetch_index()
        e = self.cat.entries_for_ui(set(), DETECTED)
        self.assertEqual((e[0]["id"], e[0]["installed"]), ("newgame", True))
        self.assertEqual(self.cat.entries_for_ui({"newgame"}, DETECTED), [])


class PluginLoading(unittest.TestCase):
    def test_downloaded_plugin_replaces_bundled_and_user_overrides_both(self):
        bundled, cat = Path(tempfile.mkdtemp()), Path(tempfile.mkdtemp())
        (bundled / "newgame.json").write_bytes(plugin(notes="bundled"))
        self.assertEqual(load_all(bundled, include_user=False, catalog_dir=cat)["newgame"].source, "bundled")
        (cat / "newgame.json").write_bytes(plugin(notes="catalog"))             # same version number on purpose
        g = load_all(bundled, include_user=False, catalog_dir=cat)["newgame"]
        self.assertEqual((g.notes, g.source), ("catalog", "catalog"))

    def test_broken_downloaded_plugin_falls_back_to_bundled(self):
        bundled, cat = Path(tempfile.mkdtemp()), Path(tempfile.mkdtemp())
        (bundled / "newgame.json").write_bytes(plugin())
        (cat / "newgame.json").write_bytes(b"{ not json")
        self.assertEqual(load_all(bundled, include_user=False, catalog_dir=cat)["newgame"].source, "bundled")

    def test_strict_schema(self):
        with self.assertRaises(Exception):
            parse_game(json.loads(plugin(unknown=1)))
        with self.assertRaises(ValueError):
            parse_game(json.loads(plugin(gid="../x")))
        with self.assertRaises(ValueError):
            parse_game(json.loads(plugin(match={"shell": "calc"})))

    def test_committed_index_is_current(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
        import make_catalog
        committed = (make_catalog.GAMES / "_index.json").read_text(encoding="utf-8").replace("\r\n", "\n")
        self.assertEqual(committed, make_catalog.render(make_catalog.build()),
                         "run python tools/make_catalog.py (and bump the plugin version)")


if __name__ == "__main__":
    unittest.main()
