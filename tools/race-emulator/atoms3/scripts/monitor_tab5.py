#!/usr/bin/env python3
"""Timestamp Tab5 serial output and reconnect after USB resets/disconnects."""

import argparse
from datetime import datetime
from pathlib import Path
import re
import time

import serial
from serial.tools import list_ports


BOOT = re.compile(r"ESP-ROM|\brst:|\[APP\] ready|\[BOOT\]", re.IGNORECASE)


def timestamp():
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def find_port(original, identity):
    ports = list(list_ports.comports())
    for candidate in ports:
        if candidate.device == original:
            return candidate.device
    if identity:
        for candidate in ports:
            if (identity.serial_number and candidate.serial_number == identity.serial_number
                    and candidate.vid == identity.vid and candidate.pid == identity.pid):
                return candidate.device
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="initial Tab5 USB serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--log", type=Path, default=None)
    args = parser.parse_args()

    log_path = args.log or (Path(__file__).resolve().parents[1] / "logs" /
                            f"tab5-{datetime.now():%Y%m%d-%H%M%S}.log")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    identity = next((port for port in list_ports.comports()
                     if port.device == args.port), None)
    connection = None
    pending = bytearray()
    boot_count = 0
    last_boot = 0.0

    with log_path.open("a", encoding="utf-8", buffering=1) as output:
        def record(message):
            line = f"{timestamp()} {message}"
            print(line, flush=True)
            output.write(line + "\n")

        record(f"[MONITOR] recording {args.port} at {args.baud} baud to {log_path}")
        try:
            while True:
                if connection is None:
                    port = find_port(args.port, identity)
                    if not port:
                        time.sleep(0.5)
                        continue
                    try:
                        connection = serial.Serial(port, args.baud, timeout=0.2)
                    except (serial.SerialException, OSError):
                        time.sleep(0.5)
                        continue
                    record(f"[MONITOR] CONNECTED {port}")
                    if identity is None:
                        identity = next((item for item in list_ports.comports()
                                         if item.device == port), None)
                try:
                    chunk = connection.read(256)
                except (serial.SerialException, OSError) as error:
                    if pending:
                        record(pending.decode("utf-8", errors="replace").rstrip())
                        pending.clear()
                    record(f"[MONITOR] DISCONNECTED {connection.port}: {error}")
                    connection.close()
                    connection = None
                    continue
                if not chunk:
                    # A removed USB device may return empty reads before an exception.
                    if not find_port(args.port, identity):
                        record(f"[MONITOR] DISCONNECTED {connection.port}")
                        connection.close()
                        connection = None
                    continue
                pending.extend(chunk)
                while b"\n" in pending:
                    raw, _, remaining = pending.partition(b"\n")
                    pending = bytearray(remaining)
                    line = raw.decode("utf-8", errors="replace").rstrip("\r")
                    record(line)
                    if BOOT.search(line) and time.monotonic() - last_boot > 5:
                        boot_count += 1
                        last_boot = time.monotonic()
                        record(f"[MONITOR] BOOT MARKER #{boot_count}")
                if len(pending) > 4096:
                    record(pending.decode("utf-8", errors="replace"))
                    pending.clear()
        except KeyboardInterrupt:
            record("[MONITOR] stopped by operator")
        finally:
            if connection is not None:
                connection.close()


if __name__ == "__main__":
    main()
