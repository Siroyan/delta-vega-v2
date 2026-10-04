"""Local HTTP and WebSocket interface. Run with python -m race_emulator.web."""

import asyncio
from contextlib import asynccontextmanager
import json
from pathlib import Path

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
import uvicorn

from .controller import Controller


ROOT = Path(__file__).resolve().parents[1]
controller = Controller(ROOT)


@asynccontextmanager
async def lifespan(_app):
    yield
    controller.close()


app = FastAPI(title="race-emulator", lifespan=lifespan)


def bad_request(exc):
    if isinstance(exc, FileNotFoundError):
        raise HTTPException(404, str(exc)) from exc
    raise HTTPException(400, str(exc)) from exc


@app.get("/")
def index():
    return FileResponse(ROOT / "static" / "index.html")


@app.get("/app.css")
def css():
    return FileResponse(ROOT / "static" / "app.css", media_type="text/css")


@app.get("/app.js")
def js():
    return FileResponse(ROOT / "static" / "app.js", media_type="text/javascript")


@app.get("/api/state")
def state():
    return controller.snapshot()


@app.get("/api/events")
def events(after: int = 0):
    return controller.recent(after)


@app.get("/api/cases")
def list_cases():
    return controller.cases.list()


@app.get("/api/courses")
def list_courses():
    manifest = json.loads((ROOT.parents[1] / "assets" / "course_manifest.json").read_text(encoding="utf-8"))
    return [{"id": entry["folder"], "name": entry["name"]} for entry in manifest["courses"]]


@app.get("/api/cases/{case_id}")
def get_case(case_id: str):
    try:
        return controller.cases.get(case_id)
    except (ValueError, FileNotFoundError, json.JSONDecodeError) as exc:
        bad_request(exc)


@app.put("/api/cases/{case_id}")
def save_case(case_id: str, data: dict):
    if data.get("id") != case_id:
        raise HTTPException(400, "case id must match URL")
    try:
        return controller.cases.save(data)
    except ValueError as exc:
        bad_request(exc)


@app.delete("/api/cases/{case_id}")
def delete_case(case_id: str):
    try:
        controller.cases.delete(case_id)
    except (ValueError, FileNotFoundError) as exc:
        bad_request(exc)
    return {"ok": True}


@app.post("/api/devices/{source}/connect")
def connect(source: str, data: dict):
    try:
        controller.connect(source, data.get("port"))
    except ValueError as exc:
        bad_request(exc)
    return controller.snapshot()


@app.post("/api/devices/{source}/disconnect")
def disconnect(source: str):
    try:
        controller.disconnect(source)
    except ValueError as exc:
        bad_request(exc)
    return controller.snapshot()


@app.post("/api/devices/refresh")
def refresh():
    try:
        controller.refresh()
    except RuntimeError as exc:
        bad_request(exc)
    return controller.snapshot()


@app.post("/api/runs/start")
def start(data: dict):
    try:
        return controller.start(data.get("case_id"), data.get("tab5_mode", "usb"))
    except (ValueError, FileNotFoundError) as exc:
        bad_request(exc)


@app.post("/api/runs/stop")
def stop():
    try:
        controller.stop()
    except (ValueError, RuntimeError) as exc:
        bad_request(exc)
    return controller.snapshot()["run"]


@app.websocket("/ws")
async def live(websocket: WebSocket):
    await websocket.accept()
    after = 0
    try:
        while True:
            events_now = controller.recent(after)
            if events_now:
                after = events_now[-1]["seq"]
            await websocket.send_json({"state": controller.snapshot(), "events": events_now})
            await asyncio.sleep(0.5)
    except (WebSocketDisconnect, RuntimeError):
        return


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Local Tab5 / AtomS3 test console")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    uvicorn.run(app, host="127.0.0.1", port=args.port, log_level="info")


if __name__ == "__main__":
    main()
