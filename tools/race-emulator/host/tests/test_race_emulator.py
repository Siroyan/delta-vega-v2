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
        self.available = True
        self.commands = []
        self.course = "misato_loop"
        self.upload = bytearray()
        self.upload_hash = ""

    def connected(self):
        return self.available

    def send(self, command):
        self.commands.append(command)
        if self.name == "atom" and command.startswith("course "):
            self.course = command.split()[1]
        if self.name == "atom" and command == "status":
            self.callback("atom", "line", "[SIM] mode=idle course={} lap=1/5 pulses=0 missed=0 protocol=2".format(self.course))
        if self.name == "atom" and command == "strategy none":
            self.callback("atom", "line", "[SIM PLAN] ready=1 plan=none sha256=none")
        if self.name == "atom" and command.startswith("dynamics "):
            self.callback("atom", "line", "[SIM PLAN] dynamics=ready")
        if self.name == "atom" and command.startswith("plan-begin "):
            self.upload.clear()
            self.upload_hash = command.split()[2]
            self.callback("atom", "line", "[SIM PLAN] offset=0")
        if self.name == "atom" and command.startswith("plan-chunk "):
            self.upload.extend(bytes.fromhex(command.split()[1]))
            self.callback("atom", "line", "[SIM PLAN] offset=" + str(len(self.upload)))
        if self.name == "atom" and command == "plan-end":
            self.callback("atom", "line", "[SIM PLAN] ready=1 plan=test sha256=" + self.upload_hash)
        if self.name == "tab5" and command == "status" and "start" in self.commands:
            self.callback("tab5", "line", "[STATUS] phase=1 lap=1 course=misato_loop gps_source=PORT_A sd_ready=1 pulses=0")
        self.callback(self.name, "command", command)

    def close(self):
        pass


class CaseTests(unittest.TestCase):
    def test_legacy_case_and_motion_parameters(self):
        self.assertEqual(validate_case(CASE)["motion_mode"], "constant")
        for values in ({"motion_mode": "strategy"}, {"acceleration_mps2": 0},
                       {"coast_deceleration_mps2": float("nan")}, {"initial_speed_kmh": 61}):
            with self.subTest(values=values), self.assertRaises(ValueError):
                validate_case(dict(CASE, **values))

    def test_strategy_transfer_snapshot_and_stall(self):
        with tempfile.TemporaryDirectory() as directory, patch(
                "race_emulator.controller.SerialLink", FakeLink):
            controller = Controller(directory)
            try:
                case = dict(CASE, course_id="motegi_oval_full", motion_mode="strategy",
                            strategy_file="motegi_demo.json", speed_kmh=30)
                controller.cases.save(case)
                expected = controller.strategies.load("motegi_demo.json", "motegi_oval_full")
                controller._on_serial("atom", "line", "[SIM] mode=idle course=motegi_oval_full sha256=" + expected.sha256)
                self.assertEqual(controller.statuses["atom"]["sha256"], expected.sha256)
                controller.start(case["id"], "standalone")
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and controller.run["state"] == "preparing":
                    time.sleep(0.02)
                self.assertEqual(controller.run["state"], "running", controller.run)
                self.assertEqual(bytes(controller.links["atom"].upload), expected.raw)
                self.assertEqual(controller.run["strategy_sha256"], expected.sha256)
                self.assertEqual((Path(controller.run["run_dir"]) / "strategy.json").read_bytes(), expected.raw)
                self.assertTrue(all(len(cmd) < 128 for cmd in controller.links["atom"].commands))
                controller._on_serial("atom", "line", "[SIM] STALLED course=motegi_oval_full")
                self.assertEqual(controller.run["outcome"], "stalled")
            finally:
                controller.close()

    def test_strategy_rejection_never_starts_motion(self):
        class RejectLink(FakeLink):
            def send(self, command):
                if command == "plan-end":
                    self.commands.append(command)
                    self.callback("atom", "line", "[SIM PLAN] error=SHA256 mismatch")
                else:
                    super().send(command)
        with tempfile.TemporaryDirectory() as directory, patch(
                "race_emulator.controller.SerialLink", RejectLink):
            controller = Controller(directory)
            try:
                controller.cases.save(dict(CASE, course_id="motegi_oval_full", motion_mode="strategy",
                                           strategy_file="motegi_demo.json"))
                controller.start(CASE["id"], "standalone")
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and controller.run["state"] == "preparing":
                    time.sleep(0.02)
                self.assertEqual(controller.run["outcome"], "setup_failed")
                self.assertNotIn("start", controller.links["atom"].commands)
            finally:
                controller.close()

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

    def test_standalone_tab5_needs_no_usb_and_receives_no_commands(self):
        with tempfile.TemporaryDirectory() as directory, patch(
                "race_emulator.controller.SerialLink", FakeLink):
            controller = Controller(directory)
            controller.links["tab5"].available = False
            try:
                controller.cases.save(CASE)
                with self.assertRaisesRegex(ValueError, "connect Tab5"):
                    controller.start("smoke-case", "usb")
                controller.start("smoke-case", "standalone")
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and controller.snapshot()["run"]["state"] != "running":
                    time.sleep(0.05)
                self.assertEqual(controller.snapshot()["run"]["state"], "running")
                self.assertEqual(controller.links["tab5"].commands, [])
                controller._on_serial("atom", "line", "[SIM] GOAL course=misato_loop")
                with controller.lock:
                    controller.run["atom_goal_at"] = time.monotonic() - 5
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline and controller.snapshot()["run"]["state"] != "finished":
                    time.sleep(0.05)
                result = json.loads((Path(controller.run["run_dir"]) / "result.json").read_text())
                self.assertEqual(result["tab5_mode"], "standalone")
                self.assertEqual(result["reason"], "AtomS3 GOAL; Tab5 phase=unobserved")
            finally:
                controller.close()


if __name__ == "__main__":
    unittest.main()
