#!/usr/bin/env python3
"""Atom-only USB integration check. Leaves MOTEGI demo selected and idle."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import sys
import time

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parents[2]
sys.path.insert(0, str(PROJECT.parent / "host"))
from race_emulator.controller import Controller


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    folder = PROJECT / "logs" / ("strategy-check-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    folder.mkdir(parents=True)
    controller = Controller(folder)
    original = (ROOT / "assets/strategy/motegi_demo.json").read_bytes()
    original_sha256 = hashlib.sha256(original).hexdigest()

    def wait_for(test, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if test(): return
            time.sleep(0.05)
        raise AssertionError("device confirmation timed out; " + str(controller.statuses["atom"]))

    def send(command):
        controller.links["atom"].send(command)
        time.sleep(0.15)

    try:
        controller.connect("atom", args.port)
        wait_for(lambda: controller.statuses["atom"].get("protocol") == "2")
        for command in ("stop", "course motegi_oval_full", "speed 30", "dynamics 0.3 0.03 0",
                        "strategy motegi_demo.json", "status"):
            send(command)
        raw = original
        expected = hashlib.sha256(raw).hexdigest()
        wait_for(lambda: controller.statuses["atom"].get("sha256") == expected)
        print("PASS: filesystem strategy matches original JSON SHA256", expected, flush=True)
        plan = json.loads(raw)
        plan["plan_id"] = "device-motion-check"
        plan["laps"][0]["runs"] = [{"on_s_m": 0, "off_s_m": 1},
                                    {"on_s_m": 3, "off_s_m": 4},
                                    {"on_s_m": 6, "off_s_m": 7}]
        payload = json.dumps(plan, separators=(",", ":")).encode()
        controller.strategies.save("device-check.json", payload, "motegi_oval_full")
        controller.cases.save({"schema_version": 1, "id": "device-strategy-check",
            "name": "Device strategy check", "course_id": "motegi_oval_full", "speed_kmh": 36,
            "motion_mode": "strategy", "strategy_file": "device-check.json", "initial_speed_kmh": 0,
            "acceleration_mps2": 1, "coast_deceleration_mps2": 0.3,
            "wheel_circumference_m": 1.03, "pulses_per_revolution": 1, "max_duration_s": 30,
            "actions": [{"at_s": 2, "command": "noise 1"}, {"at_s": 3, "command": "dropout 500"}]})
        controller.start("device-strategy-check", "standalone")
        wait_for(lambda: controller.run["state"] != "preparing", 15)
        assert controller.run["state"] == "running", controller.run
        send("nmea on")
        print("Device running; checking acceleration/coasting/stall...", flush=True)
        wait_for(lambda: controller.run["state"] == "finished", 30)
        assert controller.run["outcome"] == "stalled", controller.run
        send("status")
        wait_for(lambda: controller.statuses["atom"].get("mode") == "stalled")
        log = Path(controller.run["run_dir"]) / "events.jsonl"
        events = [json.loads(line) for line in log.read_text().splitlines()]
        changes = [e["message"] for e in events if e["message"].startswith(("[SIM] ENGINE", "[SIM] STALLED"))]
        assert sum("ENGINE ON" in line for line in changes) == 3, changes
        assert sum("ENGINE OFF" in line for line in changes) == 3, changes
        for line in changes: print(line, flush=True)
        rmcs = [e["message"].split("$", 1)[1] for e in events if e["message"].startswith("[NMEA] $GPRMC")]
        speeds = [float(sentence.split(",")[7]) * 1.852 for sentence in rmcs]
        assert len(speeds) > 10 and max(speeds) > 4 and any(b < a for a, b in zip(speeds, speeds[1:])), speeds
        assert controller.statuses["atom"]["missed"] == "0", controller.statuses["atom"]
        assert int(controller.statuses["atom"]["pulses"]) >= 10, controller.statuses["atom"]
        print("PASS: transfer, 3 ON/OFF pairs, NMEA speed rise/fall, pulses={}, missed=0, stalled".format(
            controller.statuses["atom"]["pulses"]), flush=True)
        print("Saved log:", log, flush=True)
    finally:
        if controller.links["atom"].connected():
            for command in ("stop", "noise 0", "dropout 0", "invalid 0", "corrupt 0", "nmea off",
                            "course motegi_oval_full", "speed 30", "dynamics 0.3 0.03 0",
                            "strategy motegi_demo.json", "status"):
                send(command)
            wait_for(lambda: controller.statuses["atom"].get("sha256") == original_sha256 and
                     controller.statuses["atom"].get("mode") == "idle" and
                     controller.statuses["atom"].get("max_speed") == "30.00")
            print("Final status:", controller.statuses["atom"], flush=True)
        controller.close()


if __name__ == "__main__":
    main()
