#!/usr/bin/env python3
"""Install one course and the updated catalog through Tab5 USB serial.

The Tab5 must be in Waiting with electrical OFF and microSD inserted. The
course files are installed first; catalog.json is replaced last. The device
reboots and selects the new course after every file is acknowledged.
"""

import argparse
import json
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parents[1]


def wait_line(device, prefix, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = device.readline().decode("utf-8", errors="replace").strip()
        if line.startswith("[COURSE") or line.startswith("[STATUS]"):
            print(line)
        if line.startswith(prefix):
            return line
    raise TimeoutError(f"No response starting with {prefix}")


def upload(device, folder, filename, data):
    checksum = zlib.crc32(data)
    device.write(f"course-upload {folder} {filename} {len(data)} {checksum}\n".encode())
    ready = wait_line(device, "[COURSE UPLOAD]", 10)
    if ready != f"[COURSE UPLOAD] ready bytes={len(data)}":
        raise RuntimeError(ready)
    # Feed the 128-byte UI receive budget without flooding USB CDC.
    for offset in range(0, len(data), 128):
        device.write(data[offset:offset + 128])
        device.flush()
        time.sleep(0.01)
    queued = wait_line(device, "[COURSE UPLOAD]", 10)
    if queued != "[COURSE UPLOAD] queued":
        raise RuntimeError(queued)
    saved = wait_line(device, "[COURSE UPLOAD]", 20)
    if not saved.startswith(f"[COURSE UPLOAD] saved {folder}/{filename} bytes="):
        raise RuntimeError(saved)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", help="Folder in assets/, e.g. misato_loop")
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="vega-course-") as staging:
        subprocess.run([sys.executable, str(ROOT / "scripts/package_courses.py"), staging],
                       check=True, stdout=subprocess.DEVNULL)
        package = Path(staging) / "vega/courses"
        catalog = json.loads((package / "catalog.json").read_text())
        match = next((x for x in catalog["courses"] if x["folder"] == args.folder), None)
        if not match:
            parser.error(f"Unknown course folder: {args.folder}")
        course_id = match["course_id"]
        with serial.Serial(args.port, 115200, timeout=0.25, write_timeout=5) as device:
            # Opening USB CDC can reset Tab5. Wait for its normal Waiting state.
            deadline = time.monotonic() + 25
            while True:
                device.write(b"status\n")
                try:
                    status = wait_line(device, "[STATUS]", 2)
                except TimeoutError:
                    if time.monotonic() >= deadline:
                        raise
                    continue
                if "phase=0 " in status and "power_phase=0 " in status and "sd_ready=1 " in status:
                    break
                raise RuntimeError("Tab5 must be Waiting, electrical OFF, with microSD ready")
            for filename in ("course.json", "map.rgb565"):
                upload(device, args.folder, filename, (package / args.folder / filename).read_bytes())
            upload(device, "root", "catalog.json", (package / "catalog.json").read_bytes())
            device.write(b"course-reboot\n")
            wait_line(device, "[COURSE] rebooting", 5)
            deadline = time.monotonic() + 30
            while True:
                device.write(b"course-list\n")
                try:
                    header = wait_line(device, "[COURSE LIST] count=", 2)
                except TimeoutError:
                    if time.monotonic() >= deadline:
                        raise
                    continue
                if header == f"[COURSE LIST] count={len(catalog['courses'])}":
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"New catalog not loaded: {header}")
                time.sleep(0.5)
            device.write(f"course-select {course_id}\n".encode())
            selected = wait_line(device, "[COURSE SELECT]", 5)
            if selected != "[COURSE SELECT] queued=1":
                raise RuntimeError(selected)
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                device.write(b"status\n")
                status = wait_line(device, "[STATUS]", 2)
                if f"course={course_id} " in status and "sd_ready=1 " in status:
                    print(f"Installed and selected {course_id}")
                    return
                time.sleep(0.3)
            raise RuntimeError("Course was uploaded but could not be selected")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, TimeoutError, subprocess.CalledProcessError) as exc:
        sys.exit(str(exc))
