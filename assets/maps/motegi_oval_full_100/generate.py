#!/usr/bin/env python3
"""Regenerate the 100-lap aging variant from the canonical Motegi asset."""

import argparse
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASE = HERE.parent / "motegi_oval_full"


def expected_files():
    course = json.loads((BASE / "course.json").read_text())
    original_id = course["course_id"]
    course["course_id"] = "motegi_oval_2025_full_100_v1"
    course["description"] = (
        "100-lap aging-test variant of motegi_oval_2025_full_v2; "
        "identical course geometry and artwork."
    )
    course["lap_count"] = 100
    course["race_sequence"] = [
        {"lap": lap, "route_id": "first_lap" if lap == 1 else
         "final_lap" if lap == 100 else "regular_lap"}
        for lap in range(1, 101)
    ]
    routes = course["routes"]
    course["race_length_m"] = round(
        routes["first_lap"]["length_m"] +
        98 * routes["regular_lap"]["length_m"] +
        routes["final_lap"]["length_m"], 6
    )
    course["source"]["aging_variant_of"] = original_id
    result = {"course.json": (json.dumps(course, indent=2, ensure_ascii=False) + "\n").encode()}
    for name in (course["render"]["background"], course["render"]["svg"]):
        result[name] = (BASE / name).read_bytes()
    raw_image = BASE / "map.rgb565"
    if raw_image.exists():
        pixels = raw_image.read_bytes()
    else:
        image_c = (BASE / "course_image.c").read_text()
        match = re.search(r"(?:static\s+const\s+)?uint8_t\s+\w+\[\]\s*=\s*\{(.*?)\};",
                          image_c, re.S)
        if not match:
            raise ValueError("Motegi RGB565 pixel array is missing")
        pixels = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)))
    if len(pixels) != 480 * 480 * 2:
        raise ValueError("Motegi image must be 480x480 RGB565")
    result["map.rgb565"] = pixels
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if generated files are stale")
    args = parser.parse_args()
    for name, data in expected_files().items():
        target = HERE / name
        if args.check:
            if not target.exists() or target.read_bytes() != data:
                raise SystemExit(f"outdated: {target}; run {__file__}")
        else:
            target.write_bytes(data)
        print(f"{'checked' if args.check else 'updated'} {target.name}")


if __name__ == "__main__":
    main()
