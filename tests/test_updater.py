import contextlib
import hashlib
import io
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from wandlike.core import updater

REPO = "me/wandlike"
URL = f"https://github.com/{REPO}/releases/download/v1.1.0/WandLike-Setup.exe"


class FakeResp(io.BytesIO):
    def __init__(self, data: bytes):
        super().__init__(data)
        self.headers = {"Content-Length": str(len(data))}


def opener_for(payload):
    @contextlib.contextmanager
    def op(url, timeout):
        yield FakeResp(payload if isinstance(payload, bytes) else json.dumps(payload).encode())
    return op


def release(tag="v1.1.0", url=URL, digest=None, **kw):
    a = {"name": "WandLike-Setup.exe", "browser_download_url": url}
    if digest:
        a["digest"] = digest
    return {"tag_name": tag, "assets": [a], "body": "notes", "html_url": "x", **kw}


class UpdaterTests(unittest.TestCase):
    def test_parse(self):
        self.assertGreater(updater.parse_version("v1.10.0"), updater.parse_version("1.9.9"))
        self.assertEqual(updater.parse_version("1.0"), (1, 0))

    def test_newer_found(self):
        r = updater.check(REPO, "1.0.0", opener=opener_for(release()))
        self.assertEqual(r["version"], "1.1.0")

    def test_same_or_older_ignored(self):
        self.assertIsNone(updater.check(REPO, "1.1.0", opener=opener_for(release())))
        self.assertIsNone(updater.check(REPO, "2.0.0", opener=opener_for(release())))

    def test_prerelease_and_draft_ignored(self):
        self.assertIsNone(updater.check(REPO, "1.0.0", opener=opener_for(release(prerelease=True))))
        self.assertIsNone(updater.check(REPO, "1.0.0", opener=opener_for(release(draft=True))))

    def test_foreign_asset_url_rejected(self):
        r = release(url="https://evil.example/WandLike-Setup.exe")
        self.assertIsNone(updater.check(REPO, "1.0.0", opener=opener_for(r)))

    def test_disabled_without_repo(self):
        self.assertIsNone(updater.check("", "1.0.0"))

    def test_download_verifies_hash(self):
        data = b"MZ" + b"x" * 5000
        good = hashlib.sha256(data).hexdigest()
        p = updater.download({"url": URL, "sha256": good}, opener=opener_for(data))
        self.assertEqual(p.read_bytes(), data)
        with self.assertRaises(RuntimeError):
            updater.download({"url": URL, "sha256": "0" * 64}, opener=opener_for(data))


if __name__ == "__main__":
    unittest.main()
