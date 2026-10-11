"""Validate and persist versioned, declarative test cases."""

import json
import math
import re
from pathlib import Path


ID = re.compile(r"^[a-z][a-z0-9-]{1,63}$")
FAULTS = {"noise", "bias", "spike", "dropout", "invalid", "corrupt", "seed"}
ARITY = {"noise": 1, "bias": 2, "spike": 2, "dropout": 1,
         "invalid": 1, "corrupt": 1, "seed": 1}


def _number(value, name, low, high, integer=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError("{} must be a number".format(name))
    if not math.isfinite(value) or value < low or value > high:
        raise ValueError("{} must be between {} and {}".format(name, low, high))
    if integer and int(value) != value:
        raise ValueError("{} must be an integer".format(name))
    return int(value) if integer else float(value)


def validate_command(command):
    if not isinstance(command, str):
        raise ValueError("action command must be text")
    parts = command.split()
    if not parts or parts[0] not in FAULTS or len(parts) != ARITY.get(parts[0], -1) + 1:
        raise ValueError("action must be an AtomS3 GPS fault command")
    try:
        numbers = [float(item) for item in parts[1:]]
    except ValueError:
        raise ValueError("action arguments must be numeric")
    verb = parts[0]
    bounds = {"noise": (0, 1000), "bias": (-10000, 10000),
              "spike": (-10000, 10000), "dropout": (0, 3600000),
              "invalid": (0, 3600000), "corrupt": (0, 10000),
              "seed": (1, 4294967295)}
    low, high = bounds[verb]
    for value in numbers:
        _number(value, verb, low, high, verb in {"dropout", "invalid", "corrupt", "seed"})
    return " ".join(parts)


def validate_case(raw):
    if not isinstance(raw, dict) or raw.get("schema_version") != 1:
        raise ValueError("schema_version must be 1")
    case_id = raw.get("id")
    if not isinstance(case_id, str) or not ID.fullmatch(case_id):
        raise ValueError("id must use lowercase letters, numbers and hyphens")
    name = raw.get("name")
    if not isinstance(name, str) or not name.strip() or len(name) > 100:
        raise ValueError("name must be 1–100 characters")
    description = raw.get("description", "")
    if not isinstance(description, str) or len(description) > 500:
        raise ValueError("description is too long")
    course_id = raw.get("course_id")
    if not isinstance(course_id, str) or not re.fullmatch(r"[a-z][a-z0-9_]{1,63}", course_id):
        raise ValueError("invalid course_id")
    speed = _number(raw.get("speed_kmh"), "speed_kmh", 1, 60)
    wheel = _number(raw.get("wheel_circumference_m"), "wheel_circumference_m", 0.1, 10)
    ppr = _number(raw.get("pulses_per_revolution"), "pulses_per_revolution", 1, 100, True)
    maximum = _number(raw.get("max_duration_s"), "max_duration_s", 1, 86400, True)
    actions = raw.get("actions", [])
    if not isinstance(actions, list) or len(actions) > 200:
        raise ValueError("actions must be a list of at most 200 entries")
    clean_actions = []
    for item in actions:
        if not isinstance(item, dict):
            raise ValueError("each action must be an object")
        at = _number(item.get("at_s"), "at_s", 0, maximum, True)
        clean_actions.append({"at_s": at, "command": validate_command(item.get("command"))})
    clean_actions.sort(key=lambda item: item["at_s"])
    return {"schema_version": 1, "id": case_id, "name": name.strip(),
            "description": description.strip(), "course_id": course_id,
            "speed_kmh": speed, "wheel_circumference_m": wheel,
            "pulses_per_revolution": ppr, "max_duration_s": maximum,
            "actions": clean_actions}


class CaseStore:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)

    def list(self):
        cases = []
        for path in sorted(self.directory.glob("*.json")):
            try:
                cases.append(validate_case(json.loads(path.read_text(encoding="utf-8"))))
            except (ValueError, OSError, json.JSONDecodeError) as exc:
                cases.append({"id": path.stem, "error": str(exc)})
        return cases

    def get(self, case_id):
        if not isinstance(case_id, str) or not ID.fullmatch(case_id):
            raise ValueError("invalid case id")
        path = self.directory / (case_id + ".json")
        return validate_case(json.loads(path.read_text(encoding="utf-8")))

    def save(self, raw):
        case = validate_case(raw)
        path = self.directory / (case["id"] + ".json")
        tmp = path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(case, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        tmp.replace(path)
        return case

    def delete(self, case_id):
        if not isinstance(case_id, str) or not ID.fullmatch(case_id):
            raise ValueError("invalid case id")
        (self.directory / (case_id + ".json")).unlink()
