import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
os.environ["APPDATA"] = tempfile.mkdtemp(prefix="wl-test-appdata-")

try:
    from wandlike import debug as dbg
except ImportError:  # release-only checkout
    dbg = None


@unittest.skipIf(dbg is None, "debug package not present")
class DebugTools(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import logging
        from wandlike.app.api import Api
        from wandlike.core import winapi
        cls._real_find = winapi.find_pids
        cls._handlers = list(logging.getLogger().handlers)
        cls.api = Api(dry_run=True, background=False)
        dbg.setup(cls.api)

    @classmethod
    def tearDownClass(cls):
        import logging
        from wandlike.core import winapi
        winapi.find_pids = cls._real_find
        root = logging.getLogger()
        for h in list(root.handlers):
            if h not in cls._handlers:
                h.close()
                root.removeHandler(h)

    def test_flag_in_bootstrap_and_extra_methods(self):
        self.assertTrue(self.api.bootstrap()["debug"])
        self.assertIn("ck3", self.api.debug_info()["games"])

    def test_calls_are_traced_to_log_file(self):
        self.api.game("ck3")
        text = "\n".join(self.api.debug_log_tail(50))
        self.assertIn("api.game", text)

    def test_fake_running_overrides_detection(self):
        from wandlike.core import winapi
        self.api.debug_fake_running("ck3", True)
        self.assertEqual(winapi.find_pids("ck3.exe"), [4242])
        self.api.debug_fake_running("ck3", None)
        self.assertNotEqual(winapi.find_pids("ck3.exe"), [4242])

    def test_fake_update_shows_in_state(self):
        self.api.debug_fake_update("9.9.9")
        self.assertEqual(self.api.state()["update"]["version"], "9.9.9")

    def test_exceptions_propagate_and_are_logged(self):
        with self.assertRaises(RuntimeError):
            self.api.debug_throw()
        self.assertIn("debug_throw", "\n".join(self.api.debug_log_tail(60)))

    def test_raw_console_line_dry_run(self):
        self.api.debug_send_console("ck3", "add_gold 1")


class DebugOffByDefault(unittest.TestCase):
    def test_regular_api_has_no_debug_surface(self):
        from wandlike.app.api import Api
        # a fresh Api (setup not called) must not expose debug methods or the flag
        a = Api(dry_run=True, background=False)
        self.assertFalse(a.bootstrap()["debug"])
        self.assertFalse(any(n.startswith("debug_") for n in dir(a)))


if __name__ == "__main__":
    unittest.main()
