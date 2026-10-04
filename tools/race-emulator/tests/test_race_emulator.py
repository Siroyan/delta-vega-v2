import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from race_emulator.cases import CaseStore, validate_case
from race_emulator.controller import Controller


CASE = {"schema_version": 1, "id": "smoke-case", "name": "Smoke",
        "description": "", "course_id": "misato_loop", "speed_kmh": 12,
        "wheel_circumference_m": 1.03, "pulses_per_revolution": 1,
        "max_duration_s": 10,
        "actions": [{"at_s": 0, "command": "dropout 1000"}]}


class FakeLink:
    def __init__(self, name, callback):
        self.name = name
        self.callback = callback
        self.wanted = name
        self.port = name
        self.commands = []

    def connected(self):
        return True

    def send(self, command):
        self.commands.append(command)
        if self.name == "atom" and command == "status":
            self.callback("atom", "line", "[SIM] mode=idle course=misato_loop lap=1/5 pulses=0 missed=0")
        if self.name == "tab5" and command == "status" and "start" in self.commands:
            self.callback("tab5", "line", "[STATUS] phase=1 lap=1 course=misato_loop gps_source=PORT_A sd_ready=1 pulses=0")
        self.callback(self.name, "command", command)

    def close(self):
        pass


class CaseTests(unittest.TestCase):
    def test_case_store_and_fault_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            store = CaseStore(directory)
            stored = store.save(CASE)
            self.assertEqual(store.get("smoke-case"), stored)
            self.assertEqual(len(store.list()), 1)
            self.assertTrue((Path(directory) / "smoke-case.json").exists())
            invalid = dict(CASE, actions=[{"at_s": 0, "command": "ignite"}])
            with self.assertRaises(ValueError):
                validate_case(invalid)
            with self.assertRaises(ValueError):
                store.get("../secrets")

    def test_run_records_both_devices_and_timed_fault(self):
        with tempfile.TemporaryDirectory() as directory, patch(
                "race_emulator.controller.SerialLink", FakeLink):
            controller = Controller(directory)
            try:
                controller.cases.save(CASE)
                controller._on_serial(
                    "tab5", "line",
                    "[STATUS] phase=0 lap=0 course=misato_loop gps_source=PORT_A sd_ready=1 pulses=0")
                controller.start("smoke-case")
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    if "dropout 1000" in controller.links["atom"].commands:
                        break
                    time.sleep(0.05)
                self.assertIn("start", controller.links["tab5"].commands)
                self.assertIn("start", controller.links["atom"].commands)
                self.assertIn("dropout 1000", controller.links["atom"].commands)
                controller._on_serial("tab5", "line", "[BOOT] Tab5 reboot")
                controller._on_serial("atom", "line", "[SIM] GOAL course=misato_loop")
                controller.stop()
                run_dir = Path(controller.snapshot()["run"]["run_dir"])
                events = [json.loads(line) for line in (run_dir / "events.jsonl").read_text().splitlines()]
                self.assertTrue(any(e["source"] == "tab5" and e["kind"] == "boot" for e in events), events)
                self.assertTrue(any(e["source"] == "host" and e["kind"] == "action" for e in events))
                self.assertEqual(json.loads((run_dir / "result.json").read_text())["outcome"], "stopped")
            finally:
                controller.close()


if __name__ == "__main__":
    unittest.main()
