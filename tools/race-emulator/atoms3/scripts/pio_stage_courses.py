"""Run staging before both firmware and filesystem PlatformIO targets."""

Import("env")

import sys
from pathlib import Path

sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "scripts"))
from stage_courses import stage_courses

stage_courses()
