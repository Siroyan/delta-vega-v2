import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from race_emulator.strategies import REPOSITORY, StrategyStore


class StrategyTests(unittest.TestCase):
    def test_exact_bytes_import_and_course_matching(self):
        with tempfile.TemporaryDirectory() as folder:
            store = StrategyStore(folder)
            raw = (REPOSITORY / "assets/strategy/motegi_demo.json").read_bytes()
            original = store.load("motegi_demo.json", "motegi_oval_full")
            self.assertEqual(original.raw, raw)
            self.assertEqual(store.list("motegi_oval_full")[0]["plan_id"], "dummy-race-002")
            imported = store.save("my-strategy.json", raw, "motegi_oval_full")
            self.assertEqual(store.load("my-strategy.json", "motegi_oval_full").sha256, original.sha256)
            self.assertEqual(imported["sha256"], original.sha256)
            with self.assertRaises(ValueError):
                store.save("wrong.json", raw, "tobitakyu_loop")
            with self.assertRaises(ValueError):
                store.load("../motegi_demo.json", "motegi_oval_full")

    def test_many_runs_and_invalid_strategy_fields(self):
        with tempfile.TemporaryDirectory() as folder:
            store = StrategyStore(folder)
            value = store.load("motegi_demo.json", "motegi_oval_full").data
            value["laps"][0]["runs"] = [{"on_s_m": n * 2, "off_s_m": n * 2 + 1} for n in range(8)]
            encode = lambda: json.dumps(value, separators=(",", ":")).encode()
            store.validate("eight.json", encode(), "motegi_oval_full")
            value["laps"][0]["runs"][-1]["off_s_m"] = 0
            with self.assertRaises(ValueError):
                store.validate("eight.json", encode(), "motegi_oval_full")
            with self.assertRaises(ValueError):
                store.validate("big.json", b" " * 4097, "motegi_oval_full")
            with self.assertRaises(ValueError):
                store.validate("duplicate.json", b'{"x":1,"x":2}', "motegi_oval_full")
            with self.assertRaises(ValueError):
                store.list({"folder": "motegi_oval_full"})


if __name__ == "__main__":
    unittest.main()
