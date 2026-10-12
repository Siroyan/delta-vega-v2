"""Local HTTP UI for editing firmware-compatible driving strategies."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
from urllib.parse import unquote, urlsplit

from .model import Catalog, parse_import, validate_strategy


STATIC = Path(__file__).resolve().parent / "static"
MAX_REQUEST_BYTES = 64 * 1024


class Handler(BaseHTTPRequestHandler):
    catalog = None

    def log_message(self, fmt, *args):
        print("[strategy-editor] " + fmt % args)

    def respond(self, status, body, content_type="application/json; charset=utf-8", *, filename=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        if filename:
            self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
        self.end_headers()
        self.wfile.write(body)

    def fail(self, status, message):
        self.respond(status, {"errors": [message]})

    def route(self):
        return unquote(urlsplit(self.path).path)

    def do_GET(self):
        path = self.route()
        if path == "/api/courses":
            return self.respond(200, [item.summary() for item in self.catalog.courses.values()])
        if path.startswith("/api/courses/"):
            folder = path.removeprefix("/api/courses/")
            course = self.catalog.courses.get(folder)
            if course:
                return self.respond(200, {"summary": course.summary(), "data": course.data})
        if path == "/api/examples":
            return self.respond(200, self.catalog.examples_list())
        if path.startswith("/api/examples/"):
            filename = path.removeprefix("/api/examples/")
            if filename in {item["filename"] for item in self.catalog.examples_list()}:
                return self.respond(200, (self.catalog.examples / filename).read_bytes(),
                                    "application/json; charset=utf-8")
        if path.startswith("/maps/"):
            pieces = path.split("/")
            if len(pieces) == 4:
                course = self.catalog.courses.get(pieces[2])
                if course and pieces[3] == course.image.name:
                    return self.respond(200, course.image.read_bytes(), "image/png")
        static = {"/": ("index.html", "text/html; charset=utf-8"),
                  "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                  "/style.css": ("style.css", "text/css; charset=utf-8")}
        if path in static:
            name, mime = static[path]
            return self.respond(200, (STATIC / name).read_bytes(), mime)
        self.fail(404, "見つかりません。")

    def do_POST(self):
        path = self.route()
        if path not in ("/api/import", "/api/validate", "/api/export"):
            return self.fail(404, "見つかりません。")
        try:
            size = int(self.headers.get("Content-Length", "-1"))
            if size < 0 or size > MAX_REQUEST_BYTES:
                return self.fail(413, "入力ファイルが大きすぎます。")
            raw = self.rfile.read(size)
            if path == "/api/import":
                value = parse_import(raw)
                return self.respond(200, {"strategy": value})
            request = parse_import(raw)
            if not isinstance(request, dict):
                return self.fail(400, "入力形式が正しくありません。")
            errors, normalized, payload = validate_strategy(
                request.get("strategy"), self.catalog, request.get("filename"))
            if errors:
                return self.respond(200 if path == "/api/validate" else 400,
                                    {"errors": errors, "bytes": None})
            if path == "/api/export":
                return self.respond(200, payload, "application/json; charset=utf-8",
                                    filename=request["filename"])
            return self.respond(200, {"errors": [], "bytes": len(payload),
                                      "strategy": normalized})
        except (ValueError, KeyError, TypeError) as exc:
            self.fail(400, str(exc))


def run(host="127.0.0.1", port=8766, catalog=None):
    handler = type("StrategyHandler", (Handler,), {"catalog": catalog or Catalog()})
    server = ThreadingHTTPServer((host, port), handler)
    print(f"Strategy Editor: http://{host}:{port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


def main():
    parser = argparse.ArgumentParser(description="Tab5 driving strategy editor")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8766)
    args = parser.parse_args()
    run(args.host, args.port)


if __name__ == "__main__":
    main()
