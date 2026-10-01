#!/usr/bin/env python3
"""Install a validated strategy JSON on a connected Tab5 without removing its SD card."""

import argparse
import sys
import time
from pathlib import Path

import serial


def wait_line(port: serial.Serial, prefix: str, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if line.startswith("[PLAN"):
            print(line)
        if line.startswith(prefix):
            return line
    raise TimeoutError(f"Timed out waiting for {prefix}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json_file", type=Path)
    parser.add_argument("--port", required=True, help="Tab5 serial port, e.g. /dev/cu.usbmodem1101")
    args = parser.parse_args()
    content = args.json_file.read_bytes()
    if not 0 < len(content) <= 4096:
        parser.error("Strategy file must be 1–4096 bytes")
    with serial.Serial(args.port, 115200, timeout=0.25, write_timeout=5) as device:
        # Opening the USB serial port can restart the device. Wait for the SD task.
        deadline = time.monotonic() + 20
        while True:
            device.write(b"plan-status\n")
            try:
                line = wait_line(device, "[PLAN] state=", 2)
            except TimeoutError:
                if time.monotonic() >= deadline:
                    raise
                continue
            if "state=0 " not in line:
                break
            if time.monotonic() >= deadline:
                raise TimeoutError("Plan loader did not finish starting")
            time.sleep(0.5)
        device.write(f"plan-upload {len(content)}\n".encode("ascii"))
        ready = wait_line(device, "[PLAN UPLOAD]", 5)
        if "ready bytes=" not in ready:
            raise RuntimeError(ready)
        # USB CDC can drop a large burst while the display task is running.
        for start in range(0, len(content), 32):
            device.write(content[start : start + 32])
            device.flush()
            time.sleep(0.01)
        queued = wait_line(device, "[PLAN UPLOAD]", 5)
        if queued != "[PLAN UPLOAD] queued":
            raise RuntimeError(queued)
        installed = wait_line(device, "[PLAN UPLOAD]", 10)
        if not installed.startswith("[PLAN UPLOAD] installed id="):
            raise RuntimeError(installed)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, TimeoutError) as exc:
        sys.exit(str(exc))
