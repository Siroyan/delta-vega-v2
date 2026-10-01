#!/usr/bin/env python3
"""Build the EEZ project, preserving cached embedded fonts on EEZ Studio 0.29."""
import argparse
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument(
    "--studio", default="/Applications/EEZ Studio.app/Contents/MacOS/EEZ Studio"
)
args = parser.parse_args()
ui = ROOT / "src/ui"
with tempfile.TemporaryDirectory(prefix="vega-eez-fonts-") as backup:
    backup = Path(backup)
    for font in ui.glob("ui_font_*.c"):
        shutil.copy2(font, backup / font.name)
    try:
        subprocess.run(
            [
                args.studio,
                "--build-project",
                str(ROOT / "eez/delta-vega-v2.eez-project"),
            ],
            check=True,
            cwd=ROOT,
        )
    finally:
        # EEZ 0.29 CLI may remove unchanged embedded fonts as orphan files.
        # Restore only missing files, never overwrite newly generated font data.
        declarations = set(
            re.findall(r"extern const lv_font_t (\w+);", (ui / "fonts.h").read_text())
        )
        for font in backup.glob("ui_font_*.c"):
            if font.stem in declarations and not (ui / font.name).exists():
                shutil.copy2(font, ui / font.name)
