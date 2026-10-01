#!/usr/bin/env python3
import argparse, serial, time, json, re, tempfile
from pathlib import Path

parser = argparse.ArgumentParser(
    description="Standalone Tab5 integration test: resets board and drives actual GPIO45/48."
)
parser.add_argument(
    "--standalone-tab5",
    action="store_true",
    required=True,
    help="Confirm no vehicle/ECU circuit is connected",
)
parser.add_argument("--port", default="/dev/cu.usbmodem1101")
parser.add_argument(
    "--log", type=Path, default=Path(tempfile.gettempdir()) / "vega-device-check.log"
)
args = parser.parse_args()
chunks = []
with serial.Serial(args.port, 115200, timeout=0.1) as p:
    p.dtr = False
    p.rts = False

    def read(seconds):
        limit = time.monotonic() + seconds
        while time.monotonic() < limit:
            raw = p.read(p.in_waiting or 1)
            if raw:
                chunks.append(raw.decode("utf-8", "replace"))

    def command(text, seconds=0.6):
        p.write((text + "\n").encode())
        read(seconds)

    read(5)
    command("status")
    command("settings")
    initial = dict(
        (int(index), value)
        for index, value in re.findall(
            r"\[SETTINGS\] (\d+) [^=]+ = ([^\s]+)", "".join(chunks)
        )
    )
    assert len(initial) == 14, "initial settings could not be read"
    command("ui-status")
    command("ui-settings")
    command("ui-edit 0 40:00")
    command("ui-edit 1 05:55")
    command("ui-save")
    command("ui-status")
    command("ui-back")
    command("ui-start", 2)
    command("ui-status")
    command("ui-settings")
    command("ui-edit 0 38:00")
    command("ui-save")
    command("ui-status")
    command("ui-back")
    command("on", 0.15)
    command("status", 1.1)
    command("status")
    command("ignite", 0.15)
    command("status")
    command("ignite", 0.4)
    command("status")
    command("off")
    command("status")
    command("on", 1.1)
    command("ignite", 0.2)
    command("status")
    command("off")
    command("status")
    command("ui-cancel")
    command("ui-confirm-cancel")
    command("ui-status")
    command("status")
    command("log", 3)
    # Restore initial settings after testing real NVS/UI commands.
    command("ui-settings")
    command("ui-edit 0 " + initial[0])
    command("ui-edit 1 " + initial[1])
    command("ui-save")
    command("ui-status")
    command("ui-back")
    command("status")
    read(5)
s = "".join(chunks)
args.log.write_text(s)
for line in s.splitlines():
    if line.startswith(
        ("[STATUS]", "[UI]", "[APP]", "[SD]", "[BOOT]", "[SD READBACK]")
    ):
        print(line)
a = s.find("[SD READBACK BEGIN]")
b = s.find("[SD READBACK END]", a)
assert a >= 0 and b > a, "SD readback missing"
records = [json.loads(l) for l in s[a:b].splitlines()[1:] if l.startswith("{")]
assert records[0]["total_target_s"] == 2400 and records[0]["lap_target_s"][0] == 355
assert records[-1]["event"] == "end_cancelled"
assert any(r.get("event") == "ignition" for r in records)
samples = [r for r in records if "speed" in r]
assert len(samples) >= 10
assert all(r["speed"] is None and r["latitude"] is None for r in samples)
assert all(
    a["total_time_ms"] < b["total_time_ms"] for a, b in zip(samples, samples[1:])
)
assert "power_pin=1 ignition_pin=1" in s
assert "power_pin=0 ignition_pin=0" in s
assert "[APP] command=5 accepted=0" in s, "duplicate ignition was not rejected"
assert "message=SETTINGS SAVED" in s
assert "message=TIMING ACTIVE - SETTINGS LOCKED" in s
assert not re.search("Guru Meditation|panic|assert failed|abort\\(\\)", s, re.I)
print(
    "PASS: real UI actions, NVS save, measurement settings lock, GPIO preparation/pulse/OFF/rearm, SD JSON readback",
    len(samples),
    "samples",
)
