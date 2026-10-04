#!/usr/bin/env python3
"""Build the microSD /vega/courses tree from repository course assets."""
import argparse
import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("destination", type=Path, help="microSD root or staging directory")
args = parser.parse_args()
target = args.destination / "vega" / "courses"
manifest = json.loads((ROOT / "assets/course_manifest.json").read_text())
if manifest["schema_version"] != 1:
    raise ValueError("unsupported catalog schema")
target.mkdir(parents=True, exist_ok=True)
for entry in manifest["courses"]:
    folder = entry["folder"]
    if not re.fullmatch(r"[a-z0-9_]{1,40}", folder):
        raise ValueError(f"invalid folder: {folder}")
    source_dir = ROOT / "assets" / folder
    if entry["source"] != "course.json":
        raise ValueError(f"course source must be course.json: {folder}")
    course_json = source_dir / entry["source"]
    data = json.loads(course_json.read_text())
    if data["schema_version"] != 2 or not 2 <= data.get("lap_count", 7) <= 100:
        raise ValueError(f"invalid course: {course_json}")
    raw_image = source_dir / "map.rgb565"
    if raw_image.exists():
        pixels = raw_image.read_bytes()
    else:
        # Older assets store the same bytes in a generated C array.
        image_c = (source_dir / "course_image.c").read_text()
        pixels_match = re.search(r"(?:static\s+const\s+)?uint8_t\s+\w+\[\]\s*=\s*\{(.*?)\};", image_c, re.S)
        if not pixels_match:
            raise ValueError(f"RGB565 pixel array missing: {folder}")
        pixels_section = pixels_match.group(1)
        pixels = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", pixels_section))
    if len(pixels) != 480 * 480 * 2:
        raise ValueError(f"expected 480x480 RGB565 image: {folder}")
    folder_target = target / folder
    folder_target.mkdir(exist_ok=True)
    shutil.copyfile(course_json, folder_target / "course.json")
    (folder_target / "map.rgb565").write_bytes(pixels)
    entry["course_id"] = data["course_id"]
    entry.pop("source")
    print(f"{folder}: {data['course_id']} ({len(pixels)} image bytes)")
(target / "catalog.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"Install {target} on microSD")
