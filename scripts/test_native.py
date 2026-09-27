#!/usr/bin/env python3
"""Build and run the SDK-independent core with the host compiler and sanitizers."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "lib/vega_core/src"
with tempfile.TemporaryDirectory(prefix="vega-native-") as folder:
    executable = Path(folder) / "core_tests"
    sources = sorted(CORE.rglob("*.cpp"))
    command = [
        os.environ.get("CXX", "clang++"),
        "-std=c++17",
        "-g",
        "-O1",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I",
        str(CORE),
        *map(str, sources),
        str(ROOT / "test/native/core_tests.cpp"),
        "-o",
        str(executable),
    ]
    subprocess.run(command, check=True, cwd=ROOT)
    subprocess.run([str(executable)], check=True, cwd=ROOT)
