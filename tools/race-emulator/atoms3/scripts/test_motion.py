#!/usr/bin/env python3
"""Exercise the real AtomS3 model with ASan/UBSan, no hardware required."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parents[2]


def main():
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("C++ compiler required")
    with tempfile.TemporaryDirectory(prefix="atom-motion-") as folder:
        binary = Path(folder) / "motion-test"
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g", "-O1",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I", str(PROJECT / "src"), str(PROJECT / "src/strategy_motion.cpp"),
                        str(PROJECT / "tests/motion_test.cpp"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        courses = {}
        for path in (ROOT / "assets/maps").glob("*/course.json"):
            course = json.loads(path.read_text())
            courses[course["course_id"]] = course
        for path in (ROOT / "assets/strategy").glob("*.json"):
            value = json.loads(path.read_text())
            course = courses[value["course_id"]]
            lengths = [str(course["routes"][route]["length_m"]) for route in
                       ("first_lap", "regular_lap", "final_lap")]
            args = [value["course_id"], str(course.get("lap_count", len(course["race_sequence"]))), *lengths]
            subprocess.run([str(binary), str(path), *args, "accept"], check=True)
            expanded = json.loads(path.read_text())
            expanded["laps"][0]["runs"] = [{"on_s_m": n * 2, "off_s_m": n * 2 + 1} for n in range(8)]
            temporary = Path(folder) / "expanded.json"
            temporary.write_text(json.dumps(expanded, separators=(",", ":")))
            subprocess.run([str(binary), str(temporary), *args, "accept"], check=True)
            expanded["laps"][0]["runs"][-1]["off_s_m"] = 0
            temporary.write_text(json.dumps(expanded, separators=(",", ":")))
            subprocess.run([str(binary), str(temporary), *args, "reject"], check=True)
            temporary.write_bytes(path.read_bytes() + b" " * 4096)
            subprocess.run([str(binary), str(temporary), *args, "reject"], check=True)


if __name__ == "__main__":
    main()
