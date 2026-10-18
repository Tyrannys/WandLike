import ctypes
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
os.environ["APPDATA"] = tempfile.mkdtemp(prefix="wl-test-appdata-")

from wandlike.core import winapi
from wandlike.core.engine import Engine
from wandlike.core.hook import Hook, HookError
from wandlike.core.memory import Process, compile_pattern, scan_bytes
from wandlike.core.plugin import parse_game


def game_with_hook(**over):
    raw = {"id": "demo", "name": "Demo", "exe": "demo.exe", "window_title": "Demo", "launch_args": "", "console": {},
           "hook": {"symbols": {"paused": {"type": "uint8", "aob": "80 3D ?? ?? ?? ?? 00 0F 85",
                                           "rip": {"disp_at": 2, "insn_len": 7}}}},
           "cheats": [{"id": "freeze", "label": "Freeze", "kind": "toggle",
                       "memory": {"symbol": "paused", "on": 1, "off": 0, "lock": 0.05}},
                      {"id": "gold", "label": "Gold", "kind": "number", "memory": {"symbol": "paused", "type": "int32"}}]}
    raw.update(over)
    return parse_game(raw)


class Patterns(unittest.TestCase):
    def test_wildcards_and_offsets(self):
        data = b"\x00\x80\x3d\x11\x22\x33\x44\x00\x0f\x85\x90"
        self.assertEqual(scan_bytes(data, "80 3D ?? ?? ?? ?? 00 0F 85"), [1])
        self.assertEqual(scan_bytes(data, "AA BB"), [])

    def test_bad_patterns(self):
        for bad in ("", "ZZ", "80 3"):
            with self.assertRaises(ValueError):
                compile_pattern(bad)


class HookSchema(unittest.TestCase):
    def test_valid_and_unknown_symbol_rejected(self):
        game_with_hook()
        with self.assertRaises(ValueError):
            game_with_hook(cheats=[{"id": "x", "label": "X", "kind": "toggle", "memory": {"symbol": "nope", "on": 1}}])

    def test_symbol_needs_a_locator(self):
        with self.assertRaises(ValueError):
            game_with_hook(hook={"symbols": {"a": {"type": "uint8"}}})

    def test_unknown_symbol_keys_rejected(self):
        with self.assertRaises(ValueError):
            game_with_hook(hook={"symbols": {"a": {"aob": "90", "evil": 1}}})


class HookResolution(unittest.TestCase):
    def test_rip_target_math(self):
        g = game_with_hook()
        h = Hook(g)

        class Stub:
            def read(self, addr, n):
                assert (addr, n) == (0x1000 + 2, 4)
                return struct.pack("<i", 0x500)
        h.proc = Stub()
        # insn at 0x1000, length 7, disp 0x500 -> 0x1000 + 7 + 0x500
        self.assertEqual(h._target(0x1000, g.hook["symbols"]["paused"]), 0x1507)

    def test_unresolved_symbol_gives_clear_error(self):
        h = Hook(game_with_hook(), lambda m: None)
        h.proc = object()   # pretend attached
        h.unresolved = {"paused": "byte pattern not found: this game version isn't supported yet"}
        with self.assertRaises(HookError) as cm:
            h._address({"symbol": "paused"})
        self.assertIn("isn't supported yet", str(cm.exception))


@unittest.skipUnless(sys.platform == "win32", "windows only")
class RealProcess(unittest.TestCase):
    def test_pe_sections_and_alive_on_this_python(self):
        rng = winapi.module_range(os.getpid(), os.path.basename(sys.executable))
        self.assertIsNotNone(rng)
        with Process(os.getpid()) as p:
            names = [s["name"] for s in p.pe_sections(rng[0])]
            self.assertIn(".text", names)
            self.assertTrue(p.alive())
            self.assertTrue(any(True for _ in p.regions(writable_only=True)))

    def test_read_write_value_roundtrip(self):
        v = ctypes.c_uint8(0)
        with Process(os.getpid()) as p:
            p.write_value(ctypes.addressof(v), 1, "uint8")
            self.assertEqual(v.value, 1)
            self.assertEqual(p.read_value(ctypes.addressof(v), "uint8"), 1)


class EngineMemoryCheats(unittest.TestCase):
    def setUp(self):
        self.logs = []
        self.eng = Engine(game_with_hook(), self.logs.append, dry_run=True)

    def test_toggle_fails_cleanly_when_not_hooked(self):
        c = self.eng.game.cheat("freeze")
        self.assertFalse(self.eng.set_toggle(c, True))
        self.assertFalse(self.eng.toggles.get("freeze"))
        self.assertTrue(any("hook the game first" in m for m in self.logs))

    def test_toggle_and_lock_when_hooked(self):
        import time
        self.eng.hook_attach()
        c = self.eng.game.cheat("freeze")
        self.assertTrue(self.eng.set_toggle(c, True))
        time.sleep(0.3)
        self.assertGreaterEqual(sum("memory paused = 1" in m for m in self.logs), 3)   # initial write + re-assertions
        self.eng.hook_detach()
        self.assertFalse(self.eng.hooked)
        self.assertFalse(self.eng.toggles.get("freeze"))

    def test_number_requires_hook(self):
        c = self.eng.game.cheat("gold")
        self.assertFalse(self.eng.apply_number(c, 5))
        self.eng.hook_attach()
        self.assertTrue(self.eng.apply_number(c, 5))


def console_game():
    return parse_game({
        "id": "demo", "name": "Demo", "exe": "demo.exe", "window_title": "Demo", "launch_args": "",
        "console": {"requires_symbol": "console"},
        "hook": {"symbols": {"console": {"type": "uint8", "aob": "90 90"}}},
        "cheats": [
            {"id": "enable", "label": "Enable Console", "kind": "toggle",
             "memory": {"symbol": "console", "on": 1, "off": 0, "restore": True}},
            {"id": "gold", "label": "Gold", "kind": "number", "set": "gold {value}"},
            {"id": "god", "label": "God", "kind": "toggle", "on": ["god"], "off": ["god"]}]})


class ConsoleGate(unittest.TestCase):
    def setUp(self):
        self.logs = []
        self.eng = Engine(console_game(), self.logs.append, dry_run=True)

    def test_console_cheat_needs_hook_then_console(self):
        gold = self.eng.game.cheat("gold")
        self.assertEqual(self.eng.gate(gold), "hook the game first (press Hook)")
        self.eng.hook_attach()
        self.assertEqual(self.eng.gate(gold), "turn on Enable Console first")
        self.assertTrue(self.eng.set_toggle(self.eng.game.cheat("enable"), True))
        self.assertIsNone(self.eng.gate(gold))

    def test_driver_never_types_into_a_closed_console(self):
        self.eng.hook_attach()
        with self.assertRaises(RuntimeError) as cm:
            self.eng.driver.send(["gold 5"])
        self.assertIn("Enable Console", str(cm.exception))
        self.assertFalse(any("[dry-run] gold" in m for m in self.logs))

    def test_refused_cheat_queues_nothing(self):
        self.eng.hook_attach()
        self.assertFalse(self.eng.apply_number(self.eng.game.cheat("gold"), 5))
        self.assertFalse(self.eng.set_toggle(self.eng.game.cheat("god"), True))
        self.assertTrue(self.eng._q.empty())
        self.assertEqual(self.eng.last_error, "turn on Enable Console first")

    def test_restore_puts_the_original_value_back(self):
        self.eng.hook_attach()
        self.eng.hook._fake["console"] = 7                       # what the game had before we touched it
        c = self.eng.game.cheat("enable")
        self.eng.set_toggle(c, True)
        self.assertEqual(self.eng.hook.read(c.memory), 1)
        self.eng.set_toggle(c, False)
        self.assertEqual(self.eng.hook.read(c.memory), 7)

    def test_unhook_restores_and_clears_toggles(self):
        self.eng.hook_attach()
        self.eng.hook._fake["console"] = 7
        c = self.eng.game.cheat("enable")
        self.eng.set_toggle(c, True)
        self.eng.set_toggle(self.eng.game.cheat("god"), True)
        self.eng.hook_detach()
        self.assertEqual(self.eng.toggles, {})
        self.assertEqual(self.eng.hook._fake, {})                # detached: memory view cleared

    def test_turning_off_while_unhooked_is_harmless(self):
        self.assertTrue(self.eng.set_toggle(self.eng.game.cheat("god"), False))
        self.assertTrue(self.eng._q.empty())


if __name__ == "__main__":
    unittest.main()
