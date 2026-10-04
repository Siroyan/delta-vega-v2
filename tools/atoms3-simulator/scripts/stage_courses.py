#!/usr/bin/env python3
"""Stage the exact repository course JSON files for AtomS3 LittleFS."""

import json
import re
import shutil
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parents[1]
MANIFEST = ROOT / "assets" / "course_manifest.json"
DATA = PROJECT / "data"


def stage_courses():
    catalog = json.loads(MANIFEST.read_text())
    if catalog.get("schema_version") != 1:
        raise ValueError("unsupported course catalog schema")
    courses = catalog.get("courses", [])
    if not 1 <= len(courses) <= 8:
        raise ValueError("AtomS3 supports 1 to 8 courses")

    expected = {Path("catalog.json"): MANIFEST}
    for entry in courses:
        folder = entry["folder"]
        if not re.fullmatch(r"[a-z0-9_]{1,40}", folder):
            raise ValueError(f"invalid course folder: {folder}")
        if entry["source"] != "course.json":
            raise ValueError(f"course source must be course.json: {folder}")
        source = ROOT / "assets" / folder / "course.json"
        course = json.loads(source.read_text())
        if course.get("schema_version") != 2:
            raise ValueError(f"unsupported course schema: {source}")
        expected[Path(folder) / "course.json"] = source

    DATA.mkdir(exist_ok=True)
    for target in DATA.rglob("*"):
        if target.is_file() and target.relative_to(DATA) not in expected:
            target.unlink()
    for relative, source in expected.items():
        target = DATA / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != source.read_bytes():
            shutil.copyfile(source, target)
    print(f"Staged {len(courses)} unmodified course JSON files for AtomS3 LittleFS")


if __name__ == "__main__":
    stage_courses()
