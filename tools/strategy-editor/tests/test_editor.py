"""Compatibility and HTTP tests for the local strategy editor."""

from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
from threading import Thread
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from strategy_editor.model import Catalog, EXAMPLES, parse_import, validate_strategy
from strategy_editor.web import Handler

ROOT = Path(__file__).resolve().parents[3]
CORE = ROOT / "lib" / "vega_core" / "src"


def simple_strategy(course):
    return {
        "schema_version": 1,
        "course_id": course.id,
        "plan_id": "test-001",
        "plan_type": "race",
        "laps": [
            {"lap": number, "route_id": "first_lap" if number == 1 else
             "final_lap" if number == course.lap_count else "regular_lap",
             "runs": [{"on_s_m": 0, "off_s_m": 10}]}
            for number in range(1, course.lap_count + 1)
        ],
    }


class ModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = Catalog()

    def test_bundled_strategies_round_trip(self):
        for path in EXAMPLES.glob("*.json"):
            with self.subTest(path=path.name):
                imported = parse_import(path.read_bytes())
                errors, normalized, payload = validate_strategy(imported, self.catalog, path.name)
                self.assertEqual(errors, [])
                self.assertEqual(parse_import(payload), normalized)
                self.assertLessEqual(len(payload), 4096)

    def test_100_laps_cannot_export(self):
        course = self.catalog.courses["motegi_oval_full_100"]
        errors, _, payload = validate_strategy(simple_strategy(course), self.catalog, "aging.json")
        self.assertIn("7周まで", " ".join(errors))
        self.assertIsNone(payload)

    def test_invalid_intervals_are_rejected(self):
        course = self.catalog.courses["misato_loop"]
        plan = simple_strategy(course)
        self.assertEqual(validate_strategy(plan, self.catalog, "test.json")[0], [])
        plan["laps"][0]["runs"].append({"on_s_m": 5, "off_s_m": 20})
        self.assertTrue(validate_strategy(plan, self.catalog, "test.json")[0])
        plan["laps"][0]["runs"] = [{"on_s_m": 0, "off_s_m": 200}]
        self.assertTrue(validate_strategy(plan, self.catalog, "test.json")[0])

    def test_invalid_names_and_duplicate_json_are_rejected(self):
        course = self.catalog.courses["misato_loop"]
        plan = simple_strategy(course)
        self.assertTrue(validate_strategy(plan, self.catalog, "../bad.json")[0])
        plan["plan_id"] = "bad id"
        self.assertTrue(validate_strategy(plan, self.catalog, "safe.json")[0])
        with self.assertRaises(ValueError):
            parse_import(b'{"plan_id":"a","plan_id":"b"}')
        with self.assertRaises(ValueError):
            parse_import(b'{"x":NaN}')

    def test_no_exponent_in_export(self):
        course = self.catalog.courses["misato_loop"]
        plan = simple_strategy(course)
        plan["laps"][0]["runs"][0]["off_s_m"] = 1e-5
        errors, _, payload = validate_strategy(plan, self.catalog, "test.json")
        self.assertEqual(errors, [])
        self.assertIn(b"0.00001", payload)
        self.assertNotIn(b"1e-05", payload)

    def test_export_is_accepted_by_firmware_parser(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            self.skipTest("C++ compiler unavailable")
        source = r'''
#include "domain/course.h"
#include "domain/strategy.h"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
int main(int argc, char **argv) {
  if (argc != 7) return 2;
  static const vega::CoursePoint points[] = {{0, 0, 0}, {1, 0, 1}};
  vega::CourseData data{points, 2, 1, {0, 0}, 1, 1, {1, 0, 0, 0, 1, 0}};
  data.id = argv[2];
  data.lap_count = std::atoi(argv[3]);
  for (int i = 0; i < 3; ++i)
    data.routes[i] = {points, 2, std::atof(argv[4 + i]), false};
  std::ifstream file(argv[1], std::ios::binary);
  std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  vega::Strategy strategy{};
  char error[120]{};
  return vega::parseStrategy(content.data(), content.size(), vega::Course(data),
                             data.id, strategy, error, sizeof(error)) ? 0 : 1;
}
'''
        with TemporaryDirectory(prefix="vega-editor-test-") as temporary:
            folder = Path(temporary)
            source_path, binary, output = (folder / "check.cpp", folder / "check", folder / "strategy.json")
            source_path.write_text(source)
            command = [compiler, "-std=c++17", "-I", str(CORE), str(source_path),
                       str(CORE / "domain/course.cpp"), str(CORE / "domain/strategy.cpp"),
                       str(CORE / "domain/types.cpp"), "-o", str(binary)]
            subprocess.run(command, check=True, cwd=ROOT, capture_output=True)
            for path in EXAMPLES.glob("*.json"):
                with self.subTest(path=path.name):
                    plan = parse_import(path.read_bytes())
                    _, _, payload = validate_strategy(plan, self.catalog, path.name)
                    output.write_bytes(payload)
                    course = self.catalog.by_id[plan["course_id"]]
                    lengths = [course.data["routes"][name]["length_m"] for name in
                               ("first_lap", "regular_lap", "final_lap")]
                    args = [str(binary), str(output), course.id, str(course.lap_count),
                            *map(str, lengths)]
                    subprocess.run(args, check=True, cwd=ROOT, capture_output=True)


class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = Catalog()
        handler = type("TestHandler", (Handler,), {"catalog": cls.catalog,
                                                   "log_message": lambda *args: None})
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        cls.thread = Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.origin = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def get(self, path):
        with urlopen(self.origin + path) as response:
            return response.read(), response.headers

    def post(self, path, data):
        request = Request(self.origin + path, data=json.dumps(data).encode(), method="POST")
        return urlopen(request)

    def test_catalog_and_map(self):
        items = json.loads(self.get("/api/courses")[0])
        self.assertEqual(len(items), 5)
        self.assertFalse(items[-1]["supported"])
        image, headers = self.get(items[0]["image_url"])
        self.assertEqual(headers.get_content_type(), "image/png")
        self.assertTrue(image.startswith(b"\x89PNG"))
        with self.assertRaises(HTTPError):
            self.get("/maps/../course_manifest.json")

    def test_validate_export_and_reimport(self):
        course = self.catalog.courses["misato_loop"]
        plan = simple_strategy(course)
        result = json.loads(self.post("/api/validate", {
            "strategy": plan, "filename": "test.json"}).read())
        self.assertEqual(result["errors"], [])
        with self.post("/api/export", {"strategy": plan, "filename": "test.json"}) as response:
            exported = response.read()
            self.assertEqual(response.headers.get_filename(), "test.json")
        request = Request(self.origin + "/api/import", data=exported, method="POST")
        with urlopen(request) as response:
            self.assertEqual(json.loads(response.read())["strategy"], plan)

    def test_invalid_export_is_blocked(self):
        plan = simple_strategy(self.catalog.courses["misato_loop"])
        plan["laps"][0]["runs"][0]["off_s_m"] = 0
        with self.assertRaises(HTTPError) as caught:
            self.post("/api/export", {"strategy": plan, "filename": "test.json"})
        self.assertEqual(caught.exception.code, 400)


if __name__ == "__main__":
    unittest.main()
