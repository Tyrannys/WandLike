import ctypes
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from wandlike.core.engine import console_lines
from wandlike.core.plugin import load_all


class PluginTests(unittest.TestCase):
    def setUp(self):
        self.g = load_all()["ck3"]

    def test_all_screenshot_cheats_present(self):
        labels = {c.label for c in self.g.cheats}
        for want in ["Set Money", "Set Prestige", "Set Piety", "Set Renown", "Set Dread", "Set Diplomacy",
                     "Set Martial", "Set Stewardship", "Set Intrigue", "Set Learning", "Set Prowess",
                     "Set King Age", "Set Stress", "Instant Construction", "Fast Culture Progress",
                     "Instant Movement", "Max County Control", "Freeze Time", "Enable Console",
                     "Set Treasury", "Set Influence"]:
            self.assertIn(want, labels)

    def test_number_templates(self):
        c = self.g.cheat
        self.assertEqual(console_lines(c("dread"), "number", 100), ["set_dread 100"])
        self.assertEqual(console_lines(c("money"), "number", 5000, "add"), ["gold 5000"])
        self.assertEqual(console_lines(c("money"), "number", 5000),
                         ["effect add_gold = { value = 5000 subtract = gold }"])
        self.assertEqual(console_lines(c("age"), "number", 30), ["age 30"])
        self.assertEqual(console_lines(c("influence"), "number", 50, "add"), ["add_influence 50"])
        self.assertEqual(console_lines(c("treasury"), "number", 50, "add"), ["change_treasury 50"])

    def test_toggles(self):
        c = self.g.cheat
        self.assertEqual(console_lines(c("instabuild"), "on"), ["instabuild"])
        self.assertEqual(console_lines(c("max_county_control"), "on"),
                         ["effect every_sub_realm_county = { change_county_control = 100 }"])

    def test_no_unformatted_braces_leak(self):
        for ch in self.g.cheats:
            for ln in ch.on + ch.off + ch.run:
                self.assertNotIn("{{", ln)


@unittest.skipUnless(sys.platform == "win32", "windows only")
class MemoryTests(unittest.TestCase):
    def test_roundtrip_own_process(self):
        from wandlike.core.memory import Process
        val = ctypes.c_int32(123)
        addr = ctypes.addressof(val)
        with Process(os.getpid()) as p:
            spec = {"offsets": [addr], "type": "int32"}
            self.assertEqual(p.get(spec), 123)
            p.set(spec, 999)
            self.assertEqual(val.value, 999)
            # pointer chain: ptr -> val
            ptr = ctypes.c_uint64(addr - 8)
            spec2 = {"offsets": [ctypes.addressof(ptr), 8], "type": "int32"}
            self.assertEqual(p.get(spec2), 999)


if __name__ == "__main__":
    unittest.main()
