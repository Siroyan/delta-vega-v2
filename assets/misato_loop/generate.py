#!/usr/bin/env python3
"""Regenerate the MISATO course from selected OSM road-centreline nodes.

Only the constants in this file are map-specific. The generated RGB565 file is
read directly by the generic microSD packager; no course code is compiled in.
"""

import json
import math
import struct
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
START = (36.158741, 139.163142)
LAP = (36.158129, 139.162450)
GOAL = (36.158181, 139.163041)
ORIGIN = (36.1584, 139.1628)
NORTH_SCALE = 111195.0
EAST_SCALE = NORTH_SCALE * math.cos(math.radians(ORIGIN[0]))

# OSM ways 120813336, 500802680 and 120813365. The order below runs
# counterclockwise: south from LAP, east along the bottom, north on the east,
# then west across the top and south on the west back to LAP.
NODES = {
    "lap_south": (36.1581232, 139.1624834),
    "southwest": (36.1580477, 139.1625788),
    "bottom1": (36.1580808, 139.1626983),
    "bottom2": (36.1581492, 139.162948),
    "southeast": (36.1582307, 139.1631198),
    "east1": (36.1583507, 139.1632876),
    "east2": (36.1585214, 139.163414),
    "northeast": (36.1586314, 139.1633253),
    "north1": (36.1588767, 139.1629553),
    "north2": (36.1588886, 139.1628664),
    "north3": (36.1588627, 139.162743),
    "north4": (36.158773, 139.1626591),
    "north5": (36.1585638, 139.1625776),
    "west1": (36.1584347, 139.1624238),
    "west2": (36.1582828, 139.162353),
    "west3": (36.1581971, 139.16239),
}


def en(point):
    return ((point[1] - ORIGIN[1]) * EAST_SCALE,
            (point[0] - ORIGIN[0]) * NORTH_SCALE)


def project(point, a, b):
    x, y = en(point)
    ax, ay = en(a)
    bx, by = en(b)
    fraction = max(0.0, min(1.0, ((x - ax) * (bx - ax) + (y - ay) * (by - ay)) /
                             ((bx - ax) ** 2 + (by - ay) ** 2)))
    return (a[0] + fraction * (b[0] - a[0]),
            a[1] + fraction * (b[1] - a[1]))


start_road = project(START, NODES["northeast"], NODES["north1"])
lap_road = project(LAP, NODES["lap_south"], NODES["west3"])
goal_road = project(GOAL, NODES["bottom2"], NODES["southeast"])


def path(points):
    result = []
    length = 0.0
    previous = None
    for geo in points:
        east, north = en(geo)
        if previous:
            length += math.hypot(east - previous[0], north - previous[1])
        result.append({"s_m": round(length, 6), "east_m": round(east, 6),
                       "north_m": round(north, 6)})
        previous = (east, north)
    return {"from": "", "to": "", "closed": False,
            "length_m": round(length, 6), "points": result}


south = [lap_road, NODES["lap_south"], NODES["southwest"]]
bottom = [NODES["bottom1"], NODES["bottom2"], NODES["southeast"]]
east = [NODES["east1"], NODES["east2"], NODES["northeast"]]
northwest = [start_road] + [NODES[name] for name in (
    "north1", "north2", "north3", "north4", "north5", "west1", "west2", "west3")]
regular_points = south + bottom + east + northwest + [lap_road]
first_points = northwest + [lap_road]
final_points = south + [NODES["bottom1"], NODES["bottom2"], goal_road]
regular = path(regular_points)
first = path(first_points)
final = path(final_points)
for route, start_name, end_name in ((first, "start", "lap_update"),
                                    (regular, "lap_update", "lap_update"),
                                    (final, "lap_update", "goal")):
    route["from"] = start_name
    route["to"] = end_name

all_xy = [en(p) for p in regular_points + [START, LAP, GOAL]]
min_x = min(p[0] for p in all_xy)
max_x = max(p[0] for p in all_xy)
min_y = min(p[1] for p in all_xy)
max_y = max(p[1] for p in all_xy)
scale = min(390 / (max_x - min_x), 390 / (max_y - min_y))
offset_x = 240 - scale * (min_x + max_x) / 2
offset_y = 240 + scale * (min_y + max_y) / 2


def pixel(point):
    east, north = en(point)
    return (scale * east + offset_x, -scale * north + offset_y)


def landmark(point):
    x, y = en(point)
    px, py = pixel(point)
    return {"latitude": point[0], "longitude": point[1],
            "east_m": round(x, 6), "north_m": round(y, 6),
            "x_px": round(px, 6), "y_px": round(py, 6)}


dx = en(NODES["west3"])[0] - en(NODES["lap_south"])[0]
dy = en(NODES["west3"])[1] - en(NODES["lap_south"])[1]
size = math.hypot(dx, dy)
nx, ny = -dy / size * 12, dx / size * 12
lap_e, lap_n = en(LAP)


def from_en(east, north):
    return {"latitude": round(ORIGIN[0] + north / NORTH_SCALE, 9),
            "longitude": round(ORIGIN[1] + east / EAST_SCALE, 9)}


data = {
    "schema_version": 2,
    "course_id": "misato_loop_v1",
    "description": "Provisional counterclockwise MISATO road loop from OpenStreetMap; field calibration pending.",
    "coordinate_system": {
        "input": "WGS84 latitude/longitude degrees",
        "local": "east/north meters; first-order tangent approximation",
        "origin_lat_deg": ORIGIN[0], "origin_lon_deg": ORIGIN[1],
        "east_m_per_lon_deg": EAST_SCALE, "north_m_per_lat_deg": NORTH_SCALE,
    },
    "render": {
        "width_px": 480, "height_px": 480,
        "background": "misato_course_480.png", "svg": "misato_course.svg",
        "local_to_pixel_matrix_2x3": [[scale, 0, offset_x], [0, -scale, offset_y]],
        "display_stroke_width_px": 9,
        "stroke_represents_real_road_width": False,
        "background_rgb": "#ffffff", "stroke_rgb": "#8095a6",
    },
    "landmarks": {
        "start": landmark(START), "start_on_road": landmark(start_road),
        "lap_update": landmark(LAP), "lap_on_road": landmark(lap_road),
        "goal": landmark(GOAL), "goal_on_road": landmark(goal_road),
    },
    "segments": {"misato_loop": regular},
    "routes": {"first_lap": first, "regular_lap": regular, "final_lap": final},
    "lap_count": 5,
    "race_sequence": [{"lap": i, "route_id": "first_lap" if i == 1 else
                       "final_lap" if i == 5 else "regular_lap"} for i in range(1, 6)],
    "race_length_m": round(first["length_m"] + 3 * regular["length_m"] +
                           final["length_m"], 6),
    "source": {
        "provider": "OpenStreetMap contributors", "license": "ODbL 1.0",
        "api_url": "https://api.openstreetmap.org/api/0.6/map?bbox=139.1605,36.1565,139.1655,36.1605",
        "retrieved_date": "2026-10-04",
        "road_way_ids": ["120813336", "500802680", "120813365"],
        "geometry_note": "Road centreline only. Verify local access, direction and GPS offsets before field use.",
    },
    "lap_update": {
        "requested_to_route_offset_m": round(math.dist(en(LAP), en(lap_road)), 3),
        "timing_gate_endpoints": [from_en(lap_e + nx, lap_n + ny),
                                  from_en(lap_e - nx, lap_n - ny)],
        "distance_origin": "lap_update",
        "note": "Illustrative line perpendicular to the counterclockwise approach; firmware detects projected crossing.",
    },
    "distance_conventions": {
        "segments": "s_m starts at LAP and increases counterclockwise.",
        "routes": "First lap starts at START; middle laps return to LAP; final route ends at GOAL shortly after LAP.",
        "finish_detection": "No separate finish branch; GOAL is on the regular loop.",
    },
}

(HERE / "course.json").write_text(json.dumps(data, indent=2) + "\n")

px_points = [pixel(p) for p in regular_points]
line = " ".join(f"{x:.2f},{y:.2f}" for x, y in px_points)
svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="480" height="480" viewBox="0 0 480 480">\n'
       f'  <rect width="480" height="480" fill="white"/>\n'
       f'  <polyline points="{line}" fill="none" stroke="#8095a6" stroke-width="9" '
       f'stroke-linejoin="round" stroke-linecap="round"/>\n'
       f'  <metadata>Road geometry: © OpenStreetMap contributors, ODbL 1.0. '
       f'Source: {data["source"]["api_url"]}</metadata>\n</svg>\n')
(HERE / "misato_course.svg").write_text(svg)


def distance_to_segment(x, y, a, b):
    vx, vy = b[0] - a[0], b[1] - a[1]
    t = max(0.0, min(1.0, ((x - a[0]) * vx + (y - a[1]) * vy) / (vx * vx + vy * vy)))
    return math.hypot(x - a[0] - t * vx, y - a[1] - t * vy)


rgb = bytearray()
rgb565 = bytearray()
stroke = (0x80, 0x95, 0xA6)
for y in range(480):
    for x in range(480):
        # Sample around the 9 px centreline for smooth edges on the Tab5.
        samples = sum(min(distance_to_segment(x + sx, y + sy, a, b)
                          for a, b in zip(px_points, px_points[1:])) <= 4.5
                      for sy in (0.25, 0.75) for sx in (0.25, 0.75))
        values = tuple(round((255 * (4 - samples) + channel * samples) / 4)
                       for channel in stroke)
        rgb.extend(values)
        r, g, b = values
        rgb565.extend(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))


def chunk(name, contents):
    return struct.pack(">I", len(contents)) + name + contents + struct.pack(">I", zlib.crc32(name + contents))


scanlines = b"".join(b"\0" + rgb[y * 480 * 3:(y + 1) * 480 * 3] for y in range(480))
png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 480, 480, 8, 2, 0, 0, 0)) +
       chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b""))
(HERE / "misato_course_480.png").write_bytes(png)
(HERE / "map.rgb565").write_bytes(rgb565)
print(f"MISATO: first {first['length_m']:.1f} m, regular {regular['length_m']:.1f} m, "
      f"final {final['length_m']:.1f} m, total {data['race_length_m']:.1f} m")
