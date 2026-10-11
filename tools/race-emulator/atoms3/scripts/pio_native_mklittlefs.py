"""Use the Apple Silicon Homebrew mklittlefs when PlatformIO bundles Intel-only builds."""

Import("env")

import platform
import os
import sys
from pathlib import Path

if sys.platform == "darwin" and platform.machine() == "arm64":
    native_tool = Path(os.environ.get("HOMEBREW_PREFIX", "/opt/homebrew")) / "bin/mklittlefs"
    if not native_tool.is_file():
        raise RuntimeError("Install Apple Silicon mklittlefs with: brew install mklittlefs")
    env.Replace(MKFSTOOL=str(native_tool))
    print(f"Using native mklittlefs: {native_tool}")
