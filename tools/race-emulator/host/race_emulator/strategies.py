"""Original Tab5 strategy JSON files and local user imports."""
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import re

REPOSITORY = Path(__file__).resolve().parents[4]
FILENAME = re.compile(r"[A-Za-z0-9_-]{1,30}\.json\Z")
IDENTIFIER = re.compile(r"[A-Za-z0-9_.-]{1,39}\Z")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate strategy field: " + key)
        result[key] = value
    return result


def invalid_constant(value):
    raise ValueError("invalid number: " + value)


def decimal_float(token):
    if "e" in token.lower() or len(token) >= 40:
        raise ValueError("use ordinary decimal numbers with fewer than 40 characters")
    return float(token)


def target(value):
    return type(value) is int and 1 <= value <= 86400


@dataclass(frozen=True)
class StrategyFile:
    filename: str
    raw: bytes
    data: dict

    @property
    def sha256(self):
        return hashlib.sha256(self.raw).hexdigest()

    def summary(self):
        return {"filename": self.filename, "plan_id": self.data["plan_id"],
                "plan_type": self.data.get("plan_type", "demo"), "sha256": self.sha256,
                "total_target_s": self.data.get("total_target_s")}


class StrategyStore:
    def __init__(self, local, repository=REPOSITORY):
        self.local = Path(local)
        self.repository = Path(repository)
        self.local.mkdir(parents=True, exist_ok=True)

    def course(self, folder):
        if not isinstance(folder, str):
            raise ValueError("select a course")
        maps = self.repository / "assets/maps"
        manifest = json.loads((maps / "course_manifest.json").read_text())
        if folder not in {entry["folder"] for entry in manifest["courses"]}:
            raise ValueError("unknown course")
        course = json.loads((maps / folder / "course.json").read_text())
        course.setdefault("lap_count", len(course["race_sequence"]))
        return course

    def validate(self, filename, raw, folder):
        if not isinstance(filename, str) or not FILENAME.fullmatch(filename):
            raise ValueError("invalid strategy filename")
        if not 1 <= len(raw) <= 4096:
            raise ValueError("strategy JSON must be 1..4096 bytes")
        course = self.course(folder)
        data = json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object,
                          parse_constant=invalid_constant, parse_float=decimal_float)
        if not isinstance(data, dict):
            raise ValueError("strategy must be an object")
        schema = data.get("schema_version")
        fields = {"schema_version", "course_id", "plan_id", "plan_type", "laps"}
        if type(schema) is not int or schema not in (1, 2):
            raise ValueError("strategy schema must be 1 or 2")
        if schema == 2:
            fields.add("total_target_s")
            if not target(data.get("total_target_s")):
                raise ValueError("invalid strategy total target")
        if schema == 1 and data.get("plan_id") == "dummy-race-001" and "plan_type" not in data:
            fields.remove("plan_type")
        if set(data) != fields or data.get("course_id") != course["course_id"]:
            raise ValueError("strategy fields or course_id do not match the selected course")
        if not isinstance(data.get("plan_id"), str) or not IDENTIFIER.fullmatch(data["plan_id"]):
            raise ValueError("invalid strategy plan_id")
        if data.get("plan_type", "demo") not in ("demo", "race"):
            raise ValueError("invalid plan_type")
        laps = data.get("laps")
        if not isinstance(laps, list) or len(laps) != course["lap_count"]:
            raise ValueError("strategy must define every lap of the selected course")
        seen = set()
        for lap in laps:
            expected = {"lap", "route_id", "runs"} | ({"lap_target_s"} if schema == 2 else set())
            if not isinstance(lap, dict) or set(lap) != expected:
                raise ValueError("invalid strategy lap fields")
            number = lap["lap"]
            if type(number) is not int or not 1 <= number <= course["lap_count"] or number in seen:
                raise ValueError("invalid or duplicate strategy lap")
            seen.add(number)
            route = "first_lap" if number == 1 else "final_lap" if number == course["lap_count"] else "regular_lap"
            if lap["route_id"] != route or (schema == 2 and not target(lap["lap_target_s"])):
                raise ValueError("invalid route or lap target")
            if not isinstance(lap["runs"], list) or not lap["runs"]:
                raise ValueError("each lap needs at least one ON/OFF interval")
            previous = -1
            for run in lap["runs"]:
                if not isinstance(run, dict) or set(run) != {"on_s_m", "off_s_m"}:
                    raise ValueError("invalid interval fields")
                on, off = run["on_s_m"], run["off_s_m"]
                if any(type(value) not in (int, float) or
                       (type(value) is float and not math.isfinite(value)) for value in (on, off)) or not (
                        0 <= on < off <= course["routes"][route]["length_m"] and on > previous):
                    raise ValueError("strategy intervals must be ordered and within the route")
                previous = off
        return StrategyFile(filename, raw, data)

    def load(self, filename, folder):
        if not isinstance(filename, str) or not FILENAME.fullmatch(filename):
            raise ValueError("invalid strategy filename")
        path = self.local / filename
        if not path.is_file():
            path = self.repository / "assets/strategy" / filename
        return self.validate(filename, path.read_bytes(), folder)

    def list(self, folder):
        self.course(folder)
        files = {path.name for directory in (self.repository / "assets/strategy", self.local)
                 for path in directory.glob("*.json")}
        result = []
        for filename in sorted(files):
            try:
                result.append(self.load(filename, folder).summary())
            except (ValueError, OSError):
                continue
        return result

    def save(self, filename, raw, folder):
        strategy = self.validate(filename, raw, folder)
        temporary = self.local / (filename + ".tmp")
        temporary.write_bytes(raw)
        temporary.replace(self.local / filename)
        return strategy.summary()
