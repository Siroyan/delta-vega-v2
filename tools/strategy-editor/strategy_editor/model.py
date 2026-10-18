"""Course catalog and the strict strategy format consumed by the Tab5 firmware."""

from dataclasses import dataclass
from decimal import Decimal
import json
import math
from pathlib import Path
import re


REPOSITORY = Path(__file__).resolve().parents[3]
MAPS = REPOSITORY / "assets" / "maps"
EXAMPLES = REPOSITORY / "assets" / "strategy"
IDENTIFIER = re.compile(r"[A-Za-z0-9_.-]{1,39}\Z")
FILENAME = re.compile(r"[A-Za-z0-9_-]{1,30}\.json\Z")
MAX_STRATEGY_BYTES = 4096
MAX_STRATEGY_LAPS = 7
MAX_RUNS_PER_LAP = 3


@dataclass(frozen=True)
class Course:
    folder: str
    name: str
    data: dict
    image: Path
    total_target_s: int
    lap_target_s: int

    @property
    def id(self):
        return self.data["course_id"]

    @property
    def lap_count(self):
        return self.data.get("lap_count", 7)

    def summary(self):
        return {
            "folder": self.folder,
            "name": self.name,
            "course_id": self.id,
            "lap_count": self.lap_count,
            "supported": self.lap_count <= MAX_STRATEGY_LAPS,
            "total_target_s": self.total_target_s,
            "lap_target_s": self.lap_target_s,
            "lengths_m": {
                name: route["length_m"] for name, route in self.data["routes"].items()
            },
            "image_url": f"/maps/{self.folder}/{self.image.name}",
        }


class Catalog:
    def __init__(self, maps=MAPS, examples=EXAMPLES):
        self.maps = Path(maps)
        self.examples = Path(examples)
        manifest = json.loads((self.maps / "course_manifest.json").read_text(encoding="utf-8"))
        if manifest.get("schema_version") != 1:
            raise ValueError("unsupported course manifest")
        self.courses = {}
        self.by_id = {}
        for entry in manifest["courses"]:
            folder = entry["folder"]
            data = json.loads((self.maps / folder / "course.json").read_text(encoding="utf-8"))
            image = self.maps / folder / data["render"]["background"]
            if not image.is_file() or data["course_id"] in self.by_id or folder in self.courses:
                raise ValueError(f"invalid course assets: {folder}")
            course = Course(folder, entry["name"], data, image,
                            entry["total_target_s"], entry["lap_target_s"])
            self.courses[folder] = course
            self.by_id[course.id] = course

    def examples_list(self):
        result = []
        for path in sorted(self.examples.glob("*.json")):
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                result.append({
                    "filename": path.name,
                    "plan_id": data["plan_id"],
                    "course_id": data["course_id"],
                })
            except (KeyError, ValueError):
                continue
        return result


def route_for_lap(number, lap_count):
    if number == 1:
        return "first_lap"
    if number == lap_count:
        return "final_lap"
    return "regular_lap"


def _distance(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _number_text(value):
    """Avoid exponent notation, which the firmware's bounded JSON reader rejects."""
    if value == 0:
        return "0"
    return format(Decimal(str(value)), "f")


def _encode_strategy(strategy):
    lines = [
        "{",
        '  "schema_version": 2,',
        f'  "course_id": "{strategy["course_id"]}",',
        f'  "plan_id": "{strategy["plan_id"]}",',
        f'  "plan_type": "{strategy["plan_type"]}",',
        f'  "total_target_s": {strategy["total_target_s"]},',
        '  "laps": [',
    ]
    for index, lap in enumerate(strategy["laps"]):
        runs = ", ".join(
            '{"on_s_m": %s, "off_s_m": %s}' %
            (_number_text(run["on_s_m"]), _number_text(run["off_s_m"]))
            for run in lap["runs"]
        )
        comma = "," if index + 1 < len(strategy["laps"]) else ""
        lines.append(
            f'    {{"lap": {lap["lap"]}, "route_id": "{lap["route_id"]}", '
            f'"lap_target_s": {lap["lap_target_s"]}, '
            f'"runs": [{runs}]}}{comma}'
        )
    lines += ["  ]", "}", ""]
    return "\n".join(lines).encode("utf-8")


def validate_strategy(value, catalog, filename):
    """Return (errors, canonical strategy, firmware-ready JSON bytes)."""
    errors = []
    if not isinstance(value, dict):
        return ["戦略JSONはオブジェクトにしてください。"], None, None
    if set(value) != {"schema_version", "course_id", "plan_id", "plan_type", "total_target_s", "laps"}:
        errors.append("トップ階層には schema_version / course_id / plan_id / plan_type / total_target_s / laps を指定してください。")
    if type(value.get("schema_version")) is not int or value.get("schema_version") != 2:
        errors.append("schema_version は 2 にしてください。")
    course = catalog.by_id.get(value.get("course_id")) if isinstance(value.get("course_id"), str) else None
    if course is None:
        errors.append("収録コースの course_id を選択してください。")
    elif course.lap_count > MAX_STRATEGY_LAPS:
        errors.append(f"{course.name} は{course.lap_count}周です。Tab5の現行戦略形式は7周までです。")
    plan_id = value.get("plan_id")
    if not isinstance(plan_id, str) or not IDENTIFIER.fullmatch(plan_id):
        errors.append("plan_id は39文字以内の英数字・_・-・.で指定してください。")
    if value.get("plan_type") not in ("race", "demo"):
        errors.append("plan_type は race または demo にしてください。")
    total_target = value.get("total_target_s")
    if type(total_target) is not int or not 1 <= total_target <= 86400:
        errors.append("目標ゴールタイムは1〜86400秒で指定してください。")
    if not isinstance(filename, str) or not FILENAME.fullmatch(filename):
        errors.append("ファイル名は英数字・_・-の30文字以内と .json にしてください。")

    laps = value.get("laps")
    if not isinstance(laps, list):
        errors.append("laps は周回の配列にしてください。")
        return errors, None, None
    if course is None or course.lap_count > MAX_STRATEGY_LAPS:
        return errors, None, None
    if len(laps) != course.lap_count:
        errors.append(f"{course.name} は全{course.lap_count}周の定義が必要です。")

    normalized_laps = {}
    for entry in laps:
        if not isinstance(entry, dict) or set(entry) != {"lap", "route_id", "lap_target_s", "runs"}:
            errors.append("各周には lap / route_id / lap_target_s / runs を指定してください。")
            continue
        number = entry["lap"]
        if type(number) is not int or not 1 <= number <= course.lap_count:
            errors.append("lap は対象コースの周回番号にしてください。")
            continue
        if number in normalized_laps:
            errors.append(f"LAP {number}: 周回番号が重複しています。")
            continue
        route_id = route_for_lap(number, course.lap_count)
        if entry["route_id"] != route_id:
            errors.append(f"LAP {number}: route_id は {route_id} にしてください。")
            continue
        route = course.data["routes"].get(route_id)
        if not route:
            errors.append(f"LAP {number}: 経路データがありません。")
            continue
        lap_target = entry["lap_target_s"]
        if type(lap_target) is not int or not 1 <= lap_target <= 86400:
            errors.append(f"LAP {number}: 目標時間は1〜86400秒で指定してください。")
            continue
        runs = entry["runs"]
        if not isinstance(runs, list) or not 1 <= len(runs) <= MAX_RUNS_PER_LAP:
            errors.append(f"LAP {number}: ON/OFF区間は1〜3件にしてください。")
            continue
        previous_off = -1
        normalized_runs = []
        for run_number, run in enumerate(runs, 1):
            if not isinstance(run, dict) or set(run) != {"on_s_m", "off_s_m"}:
                errors.append(f"LAP {number} 区間{run_number}: ONとOFFの距離を指定してください。")
                continue
            on, off = run["on_s_m"], run["off_s_m"]
            if not _distance(on) or not _distance(off) or not (
                0 <= on < off <= route["length_m"] and on > previous_off
            ):
                errors.append(
                    f"LAP {number} 区間{run_number}: 0 ≤ ON < OFF ≤ {route['length_m']:.1f} m、"
                    "次のONは前のOFFより先にしてください。"
                )
                continue
            normalized_runs.append({"on_s_m": on, "off_s_m": off})
            previous_off = off
        normalized_laps[number] = {"lap": number, "route_id": route_id,
                                   "lap_target_s": lap_target, "runs": normalized_runs}

    for number in range(1, course.lap_count + 1):
        if number not in normalized_laps:
            errors.append(f"LAP {number}: 定義がありません。")
    if errors:
        return errors, None, None
    normalized = {
        "schema_version": 2,
        "course_id": course.id,
        "plan_id": plan_id,
        "plan_type": value["plan_type"],
        "total_target_s": total_target,
        "laps": [normalized_laps[number] for number in range(1, course.lap_count + 1)],
    }
    payload = _encode_strategy(normalized)
    # Reader::number in the firmware has a 39-character token buffer.
    for lap in normalized["laps"]:
        for run in lap["runs"]:
            if any(len(_number_text(run[key])) >= 40 for key in ("on_s_m", "off_s_m")):
                return ["距離の小数桁が長すぎます。数値を丸めてください。"], None, None
    if len(payload) > MAX_STRATEGY_BYTES:
        return [f"JSONが{len(payload)}バイトです。Tab5の上限4096バイトを超えています。"], None, None
    return [], normalized, payload


def parse_import(raw):
    """Parse uploaded JSON without losing duplicate keys or accepting NaN/Infinity."""
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"重複した項目: {key}")
            result[key] = value
        return result

    def bad_constant(value):
        raise ValueError(f"使用できない数値: {value}")

    if len(raw) > 65536:
        raise ValueError("読み込むファイルが大きすぎます。")
    try:
        return json.loads(raw.decode("utf-8-sig"), object_pairs_hook=unique_object,
                          parse_constant=bad_constant)
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"JSONを読めません: {exc}") from exc
