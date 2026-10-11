"""Host-side test execution, event recording and device status."""

from collections import deque
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import threading
import time

from .cases import CaseStore
from .serial_link import SerialLink, available_ports


FIELDS = re.compile(r"\b([a-z_]+)=([^ ]+)")
BOOT = re.compile(r"ESP-ROM|\brst:|\[BOOT\]", re.IGNORECASE)


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


class Controller:
    def __init__(self, root):
        self.root = Path(root)
        self.cases = CaseStore(self.root / "testcases")
        self.runs_dir = self.root / "runs"
        self.runs_dir.mkdir(exist_ok=True)
        self.lock = threading.RLock()
        self.events = deque(maxlen=2000)
        self.sequence = 0
        self.statuses = {"atom": {}, "tab5": {}}
        self.status_times = {"atom": 0.0, "tab5": 0.0}
        self.boots = {"atom": 0, "tab5": 0}
        self.last_boot = {"atom": float("-inf"), "tab5": float("-inf")}
        self.run = None
        self.event_file = None
        self.stop_event = threading.Event()
        self.links = {name: SerialLink(name, self._on_serial) for name in ("atom", "tab5")}
        self.tick_thread = threading.Thread(target=self._tick, daemon=True, name="test-scheduler")
        self.tick_thread.start()

    def _record(self, source, kind, message):
        with self.lock:
            self.sequence += 1
            event = {"seq": self.sequence, "time": utc_now(), "source": source,
                     "kind": kind, "message": message}
            self.events.append(event)
            if self.event_file:
                self.event_file.write(json.dumps(event, ensure_ascii=False) + "\n")
            return event

    def _on_serial(self, source, kind, message):
        self._record(source, kind, message)
        if kind == "connect":
            try:
                self.links[source].send("status")
            except RuntimeError:
                pass
        if kind == "disconnect" and source == "atom":
            with self.lock:
                if self.run and self.run["state"] in ("preparing", "running"):
                    self._finish("interrupted", "AtomS3 USB connection lost")
        if kind != "line":
            return
        now = time.monotonic()
        if BOOT.search(message):
            with self.lock:
                if now - self.last_boot[source] > 5:
                    self.last_boot[source] = now
                    self.boots[source] += 1
                    self._record(source, "boot", "Boot marker #{}".format(self.boots[source]))
                    if self.run and self.run["state"] == "running":
                        self._record("host", "alert", source + " rebooted during the run")
        prefix = "[SIM] mode=" if source == "atom" else "[STATUS] phase="
        if message.startswith(prefix):
            with self.lock:
                self.statuses[source] = dict(FIELDS.findall(message))
                self.status_times[source] = now
        if source == "atom" and message.startswith("[SIM] GOAL"):
            with self.lock:
                if self.run and self.run["state"] == "running":
                    self.run["atom_goal_at"] = now
                    self._record("host", "milestone", "AtomS3 reached GOAL")

    def connect(self, source, port):
        if source not in self.links:
            raise ValueError("unknown device")
        if any(link.wanted == port for name, link in self.links.items() if name != source):
            raise ValueError("port is already assigned to the other device")
        with self.lock:
            self.statuses[source] = {}
            self.status_times[source] = 0.0
        self.links[source].select(port)

    def disconnect(self, source):
        if source not in self.links:
            raise ValueError("unknown device")
        self.links[source].deselect()
        with self.lock:
            self.statuses[source] = {}
            self.status_times[source] = 0.0

    def refresh(self):
        for link in self.links.values():
            if link.connected():
                link.send("status")

    def snapshot(self):
        with self.lock:
            now = time.monotonic()
            return {"ports": available_ports(), "devices": {
                name: {"selected_port": link.wanted, "connected": link.connected(),
                       "active_port": link.port, "boot_count": self.boots[name],
                       "status": dict(self.statuses[name]),
                       "status_age_s": round(now - self.status_times[name], 1)
                       if self.status_times[name] else None}
                for name, link in self.links.items()},
                "run": self._public_run(), "seq": self.sequence}

    def _public_run(self):
        if not self.run:
            return None
        fields = ("id", "case_id", "case_name", "tab5_mode", "state", "outcome", "reason",
                  "started_at", "ended_at", "run_dir")
        result = {key: self.run.get(key) for key in fields}
        if self.run.get("start_monotonic"):
            end = self.run.get("end_monotonic") or time.monotonic()
            result["elapsed_s"] = round(end - self.run["start_monotonic"], 1)
        else:
            result["elapsed_s"] = 0
        return result

    def recent(self, after=0):
        with self.lock:
            return [event for event in self.events if event["seq"] > after]

    def start(self, case_id, tab5_mode="usb"):
        case = self.cases.get(case_id)
        if tab5_mode not in ("usb", "standalone"):
            raise ValueError("tab5_mode must be usb or standalone")
        with self.lock:
            if self.run and self.run["state"] in ("preparing", "running"):
                raise ValueError("a run is already active")
            if not self.links["atom"].connected():
                raise ValueError("connect AtomS3")
            if tab5_mode == "usb":
                if not self.links["tab5"].connected():
                    raise ValueError("connect Tab5 or select standalone mode")
                if self.statuses["tab5"].get("course") != case["course_id"]:
                    raise ValueError("select {} on Tab5 first".format(case["course_id"]))
                if self.statuses["tab5"].get("phase") != "0":
                    raise ValueError("Tab5 must be on the Waiting screen")
                if self.statuses["tab5"].get("gps_source") != "PORT_A":
                    raise ValueError("set Tab5 GPS INPUT to PORT.A")
                if self.statuses["tab5"].get("sd_ready") != "1":
                    raise ValueError("Tab5 microSD is not ready")
                if time.monotonic() - self.status_times["tab5"] > 10:
                    raise ValueError("Tab5 status is stale; refresh device status")
            stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
            run_id = "{}-{}".format(stamp, case_id)
            directory = self.runs_dir / run_id
            directory.mkdir(exist_ok=False)
            (directory / "case.json").write_text(
                json.dumps(case, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            self.event_file = (directory / "events.jsonl").open("a", encoding="utf-8", buffering=1)
            self.run = {"id": run_id, "case_id": case_id, "case_name": case["name"],
                        "tab5_mode": tab5_mode,
                        "case": case, "state": "preparing", "outcome": None, "reason": None,
                        "started_at": utc_now(), "ended_at": None,
                        "run_dir": str(directory), "next_action": 0,
                        "start_monotonic": None, "end_monotonic": None,
                        "atom_goal_at": None,
                        "initial_tab_boots": self.boots["tab5"]}
            self._record("host", "run", "Preparing " + case["name"])
        threading.Thread(target=self._prepare, args=(run_id,), daemon=True).start()
        return self.snapshot()["run"]

    def _prepare(self, run_id):
        try:
            with self.lock:
                case = self.run["case"]
            atom = self.links["atom"]
            for command in ("stop", "reset", "course " + case["course_id"],
                            "speed {:.2f}".format(case["speed_kmh"]),
                            "wheel {:.4f} {}".format(case["wheel_circumference_m"],
                                                     case["pulses_per_revolution"]),
                            "noise 0", "bias 0 0", "dropout 0", "invalid 0",
                            "corrupt 0", "nmea off", "status"):
                with self.lock:
                    if not self.run or self.run["id"] != run_id or self.run["state"] != "preparing":
                        return
                atom.send(command)
                time.sleep(0.08)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                with self.lock:
                    if not self.run or self.run["id"] != run_id or self.run["state"] != "preparing":
                        return
                    status = self.statuses["atom"]
                    fresh = time.monotonic() - self.status_times["atom"] < 5
                    if (fresh and status.get("course") == case["course_id"]
                            and status.get("mode") == "idle"):
                        break
                time.sleep(0.1)
            else:
                raise RuntimeError("AtomS3 did not confirm the selected course")
            if self.run["tab5_mode"] == "usb":
                tab = self.links["tab5"]
                tab.send("start")
                tab.send("status")
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    with self.lock:
                        if not self.run or self.run["id"] != run_id or self.run["state"] != "preparing":
                            return
                        if self.statuses["tab5"].get("phase") == "1":
                            break
                    time.sleep(0.1)
                    if time.monotonic() + 0.1 < deadline:
                        tab.send("status")
                else:
                    raise RuntimeError("Tab5 did not confirm timing start; AtomS3 remains idle")
            else:
                self._record("host", "note", "Tab5 standalone: timing and logs are not observed")
            with self.lock:
                if not self.run or self.run["id"] != run_id or self.run["state"] != "preparing":
                    return
                atom.send("start")
                self.run["start_monotonic"] = time.monotonic()
                self.run["state"] = "running"
                self._record("host", "run", "Run started")
        except (RuntimeError, ValueError, OSError) as exc:
            with self.lock:
                if self.run and self.run["id"] == run_id and self.run["state"] == "preparing":
                    self._finish("setup_failed", str(exc))

    def stop(self):
        with self.lock:
            if not self.run or self.run["state"] not in ("preparing", "running"):
                raise ValueError("no active run")
            if self.links["atom"].connected():
                try:
                    self.links["atom"].send("stop")
                except RuntimeError:
                    pass
            self._finish("stopped", "stopped by operator")

    def _finish(self, outcome, reason):
        # Caller holds the controller lock.
        if not self.run or self.run["state"] not in ("preparing", "running"):
            return
        self.run["state"] = "finished"
        self.run["outcome"] = outcome
        self.run["reason"] = reason
        self.run["ended_at"] = utc_now()
        self.run["end_monotonic"] = time.monotonic()
        self._record("host", "run", "Finished: {} ({})".format(outcome, reason))
        directory = Path(self.run["run_dir"])
        (directory / "result.json").write_text(
            json.dumps(self._public_run(), ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        if self.event_file:
            self.event_file.close()
            self.event_file = None

    def _tick(self):
        last_poll = 0.0
        while not self.stop_event.wait(0.2):
            now = time.monotonic()
            if now - last_poll > 3:
                last_poll = now
                for link in self.links.values():
                    if link.connected():
                        try:
                            link.send("status")
                        except RuntimeError:
                            pass
            with self.lock:
                run = self.run
                if not run or run["state"] != "running":
                    continue
                elapsed = now - run["start_monotonic"]
                actions = run["case"]["actions"]
                while run["next_action"] < len(actions) and actions[run["next_action"]]["at_s"] <= elapsed:
                    action = actions[run["next_action"]]
                    run["next_action"] += 1
                    try:
                        self.links["atom"].send(action["command"])
                        self._record("host", "action", "At {} s: {}".format(
                            action["at_s"], action["command"]))
                    except RuntimeError as exc:
                        self._finish("interrupted", str(exc))
                        break
                if run["state"] != "running":
                    continue
                if run["atom_goal_at"] and now - run["atom_goal_at"] >= 5:
                    phase = (self.statuses["tab5"].get("phase", "unknown")
                             if run["tab5_mode"] == "usb" else "unobserved")
                    self._finish("completed", "AtomS3 GOAL; Tab5 phase=" + phase)
                elif elapsed >= run["case"]["max_duration_s"]:
                    try:
                        self.links["atom"].send("stop")
                    except RuntimeError:
                        pass
                    self._finish("timeout", "case duration limit reached")

    def close(self):
        self.stop_event.set()
        if self.run and self.run["state"] in ("preparing", "running"):
            with self.lock:
                if self.links["atom"].connected():
                    try:
                        self.links["atom"].send("stop")
                    except RuntimeError:
                        pass
                self._finish("interrupted", "host server stopped")
        for link in self.links.values():
            link.close()
        self.tick_thread.join(timeout=2)
