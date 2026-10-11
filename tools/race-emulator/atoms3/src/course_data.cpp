#include "course_data.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sim {

Course courses[kMaxCourses]{};
uint8_t course_count = 0;

namespace {

class JsonReader {
 public:
  explicit JsonReader(File &file) : file_(file) {}

  int peek() {
    if (next_ == -2) next_ = file_.read();
    return next_;
  }
  int take() {
    const int value = peek();
    next_ = -2;
    return value;
  }
  void space() {
    while (peek() == ' ' || peek() == '\n' || peek() == '\r' || peek() == '\t') take();
  }
  bool consume(char expected) {
    space();
    if (peek() != expected) return false;
    take();
    return true;
  }
  bool finished() {
    space();
    return peek() < 0;
  }
  bool string(char *output, size_t capacity) {
    if (!consume('"')) return false;
    size_t length = 0;
    for (;;) {
      const int ch = take();
      if (ch < 0 || ch < 0x20) return false;
      if (ch == '"') {
        if (output) output[length] = '\0';
        return true;
      }
      int decoded = ch;
      if (ch == '\\') {
        decoded = take();
        if (decoded == 'u') {
          for (int i = 0; i < 4; ++i) {
            const int hex = take();
            if (!((hex >= '0' && hex <= '9') ||
                  (hex >= 'a' && hex <= 'f') || (hex >= 'A' && hex <= 'F'))) return false;
          }
          decoded = '?'; // Identifiers used by the simulator are ASCII.
        } else if (decoded == 'b' || decoded == 'f' || decoded == 'n' ||
                   decoded == 'r' || decoded == 't') {
          decoded = ' ';
        } else if (decoded != '"' && decoded != '\\' && decoded != '/') {
          return false;
        }
      }
      if (output) {
        if (length + 1 >= capacity) return false;
        output[length++] = static_cast<char>(decoded);
      }
    }
  }
  bool number(double &value) {
    space();
    char buffer[64];
    size_t length = 0;
    while ((peek() >= '0' && peek() <= '9') || peek() == '-' ||
           peek() == '+' || peek() == '.' || peek() == 'e' || peek() == 'E') {
      if (length + 1 >= sizeof(buffer)) return false;
      buffer[length++] = static_cast<char>(take());
    }
    if (!length) return false;
    buffer[length] = '\0';
    char *end = nullptr;
    value = strtod(buffer, &end);
    return end == buffer + length && std::isfinite(value);
  }
  bool integer(int &value) {
    double parsed = 0;
    if (!number(parsed) || parsed < -2147483647 || parsed > 2147483647 ||
        parsed != static_cast<int>(parsed)) return false;
    value = static_cast<int>(parsed);
    return true;
  }
  template <typename Handler> bool object(Handler handler) {
    if (!consume('{')) return false;
    space();
    if (consume('}')) return true;
    do {
      char key[64];
      if (!string(key, sizeof(key)) || !consume(':') || !handler(key)) return false;
      space();
      if (consume('}')) return true;
    } while (consume(','));
    return false;
  }
  template <typename Handler> bool array(Handler handler) {
    if (!consume('[')) return false;
    space();
    if (consume(']')) return true;
    do {
      if (!handler()) return false;
      space();
      if (consume(']')) return true;
    } while (consume(','));
    return false;
  }
  bool skip(unsigned depth = 0) {
    if (depth > 32) return false;
    space();
    if (peek() == '"') return string(nullptr, 0);
    if (peek() == '{') return object([&](const char *) { return skip(depth + 1); });
    if (peek() == '[') return array([&]() { return skip(depth + 1); });
    if (peek() == 't' || peek() == 'f' || peek() == 'n') {
      const char *word = peek() == 't' ? "true" : peek() == 'f' ? "false" : "null";
      for (const char *p = word; *p; ++p) if (take() != *p) return false;
      return true;
    }
    double ignored = 0;
    return number(ignored);
  }

 private:
  File &file_;
  int next_ = -2;
};

bool validFolder(const char *folder) {
  if (!*folder) return false;
  for (const char *p = folder; *p; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
  return true;
}

bool readPoint(JsonReader &json, Point &point) {
  bool east = false, north = false, distance = false;
  const bool parsed = json.object([&](const char *key) {
    double value = 0;
    if (!strcmp(key, "east_m") || !strcmp(key, "north_m") || !strcmp(key, "s_m")) {
      if (!json.number(value)) return false;
      if (!strcmp(key, "east_m")) { point.east_m = value; east = true; }
      else if (!strcmp(key, "north_m")) { point.north_m = value; north = true; }
      else { point.s_m = value; distance = true; }
      return true;
    }
    return json.skip();
  });
  return parsed && east && north && distance;
}

bool readRoute(JsonReader &json, Route &route) {
  bool got_length = false, got_points = false;
  uint16_t capacity = 0;
  const bool parsed = json.object([&](const char *key) {
    if (!strcmp(key, "length_m")) {
      double value = 0;
      if (!json.number(value) || value <= 0) return false;
      route.length_m = value;
      return got_length = true;
    }
    if (!strcmp(key, "points")) {
      const bool ok = json.array([&]() {
        if (route.count >= 2048) return false;
        if (route.count == capacity) {
          const size_t next_capacity = capacity ? capacity * 2 : 16;
          auto *larger = static_cast<Point *>(realloc(route.points, next_capacity * sizeof(Point)));
          if (!larger) return false;
          route.points = larger;
          capacity = next_capacity;
        }
        Point point{};
        if (!readPoint(json, point) ||
            (route.count == 0 && fabs(point.s_m) > 0.01f) ||
            (route.count && point.s_m <= route.points[route.count - 1].s_m)) return false;
        route.points[route.count++] = point;
        return true;
      });
      return got_points = ok;
    }
    return json.skip();
  });
  return parsed && got_length && got_points && route.count >= 2 &&
         fabs(route.points[route.count - 1].s_m - route.length_m) <= 0.02f;
}

bool readCoordinateSystem(JsonReader &json, Course &course) {
  bool lat = false, lon = false, east = false, north = false;
  const bool parsed = json.object([&](const char *key) {
    double value = 0;
    if (!strcmp(key, "origin_lat_deg") || !strcmp(key, "origin_lon_deg") ||
        !strcmp(key, "east_m_per_lon_deg") || !strcmp(key, "north_m_per_lat_deg")) {
      if (!json.number(value)) return false;
      if (!strcmp(key, "origin_lat_deg")) { course.origin_lat = value; lat = true; }
      else if (!strcmp(key, "origin_lon_deg")) { course.origin_lon = value; lon = true; }
      else if (!strcmp(key, "east_m_per_lon_deg")) { course.east_per_lon = value; east = true; }
      else { course.north_per_lat = value; north = true; }
      return true;
    }
    return json.skip();
  });
  return parsed && lat && lon && east && north && fabs(course.origin_lat) <= 90 &&
         fabs(course.origin_lon) <= 180 && course.east_per_lon > 0 && course.north_per_lat > 0;
}

bool readRaceSequence(JsonReader &json, uint8_t &count) {
  char previous[16]{};
  const bool parsed_sequence = json.array([&]() {
    if (count >= kMaxLaps) return false;
    char route[16]{};
    bool found_route = false, found_lap = false;
    const bool parsed = json.object([&](const char *key) {
      if (!strcmp(key, "route_id")) {
        found_route = json.string(route, sizeof(route));
        return found_route;
      }
      if (!strcmp(key, "lap")) {
        int number = 0;
        found_lap = json.integer(number) && number == count + 1;
        return found_lap;
      }
      return json.skip();
    });
    if (!parsed || !found_route || !found_lap ||
        (count && strcmp(previous, count == 1 ? "first_lap" : "regular_lap")))
      return false;
    strcpy(previous, route);
    ++count;
    return true;
  });
  return parsed_sequence && count >= 2 && !strcmp(previous, "final_lap");
}

void release(Course &course) {
  free(course.first.points);
  free(course.regular.points);
  free(course.final.points);
  course = {};
}

bool readCourseFile(const char *folder, Course &course) {
  char path[64];
  snprintf(path, sizeof(path), "/%s/course.json", folder);
  File file = LittleFS.open(path, "r");
  if (!file || file.size() > 256 * 1024) {
    Serial.printf("[SIM] cannot open %s\n", path);
    return false;
  }
  Serial.printf("[SIM] parsing %s bytes=%u free_heap=%u\n", path,
                static_cast<unsigned>(file.size()), ESP.getFreeHeap());
  JsonReader json(file);
  bool schema = false, coordinates = false, routes = false, laps = false, sequence_found = false;
  uint8_t sequence_count = 0;
  const bool parsed = json.object([&](const char *key) {
    if (!strcmp(key, "schema_version")) {
      int version = 0;
      return (schema = json.integer(version) && version == 2);
    }
    if (!strcmp(key, "coordinate_system"))
      return (coordinates = readCoordinateSystem(json, course));
    if (!strcmp(key, "lap_count")) {
      int count = 0;
      if (!json.integer(count) || count < 2 || count > kMaxLaps) return false;
      course.lap_count = count;
      return (laps = true);
    }
    if (!strcmp(key, "race_sequence"))
      return (sequence_found = readRaceSequence(json, sequence_count));
    if (!strcmp(key, "routes")) {
      bool first = false, regular = false, final = false;
      const bool ok = json.object([&](const char *route_id) {
        if (!strcmp(route_id, "first_lap")) {
          first = readRoute(json, course.first);
          if (!first) Serial.printf("[SIM] invalid first_lap at %u\n", file.position());
          return first;
        }
        if (!strcmp(route_id, "regular_lap")) {
          regular = readRoute(json, course.regular);
          if (!regular) Serial.printf("[SIM] invalid regular_lap at %u\n", file.position());
          return regular;
        }
        if (!strcmp(route_id, "final_lap")) {
          final = readRoute(json, course.final);
          if (!final) Serial.printf("[SIM] invalid final_lap at %u\n", file.position());
          return final;
        }
        return json.skip();
      });
      return (routes = ok && first && regular && final);
    }
    return json.skip();
  });
  if (!laps && sequence_found) course.lap_count = sequence_count;
  if (!parsed || !json.finished() || !schema || !coordinates || !routes ||
      !sequence_found || sequence_count < 2 || sequence_count > kMaxLaps ||
      sequence_count != course.lap_count) {
    Serial.printf("[SIM] invalid course %s offset=%u parsed=%u schema=%u coord=%u "
                  "routes=%u laps=%u sequence=%u count=%u/%u free_heap=%u\n",
                  folder, file.position(), parsed, schema, coordinates, routes, laps,
                  sequence_found, sequence_count, course.lap_count, ESP.getFreeHeap());
    return false;
  }
  return true;
}

bool readCatalogEntry(JsonReader &json, Course &course) {
  bool folder = false, source = false, name = false, short_name = false;
  const bool parsed = json.object([&](const char *key) {
    if (!strcmp(key, "folder")) return (folder = json.string(course.id, sizeof(course.id)));
    if (!strcmp(key, "source")) {
      char source_name[24];
      if (!json.string(source_name, sizeof(source_name))) return false;
      return (source = !strcmp(source_name, "course.json"));
    }
    if (!strcmp(key, "name")) return (name = json.string(course.name, sizeof(course.name)));
    if (!strcmp(key, "short_name"))
      return (short_name = json.string(course.short_name, sizeof(course.short_name)));
    return json.skip();
  });
  return parsed && folder && source && name && short_name && validFolder(course.id) &&
         readCourseFile(course.id, course);
}

void clearCourses() {
  for (auto &course : courses) release(course);
  course_count = 0;
}

} // namespace

bool loadCoursesFromFs() {
  clearCourses();
  if (!LittleFS.begin(false)) {
    Serial.println("[SIM] LittleFS mount failed; uploadfs is required");
    return false;
  }
  File file = LittleFS.open("/catalog.json", "r");
  if (!file || file.size() > 16 * 1024) {
    Serial.println("[SIM] catalog.json missing or too large");
    return false;
  }
  JsonReader json(file);
  bool schema = false, entries = false;
  const bool parsed = json.object([&](const char *key) {
    if (!strcmp(key, "schema_version")) {
      int version = 0;
      return (schema = json.integer(version) && version == 1);
    }
    if (!strcmp(key, "courses")) {
      return (entries = json.array([&]() {
        if (course_count >= kMaxCourses) return false;
        if (!readCatalogEntry(json, courses[course_count])) return false;
        Serial.printf("[SIM] loaded %s %u laps\n", courses[course_count].id,
                      courses[course_count].lap_count);
        ++course_count;
        return true;
      }));
    }
    return json.skip();
  });
  if (!parsed || !json.finished() || !schema || !entries || !course_count) {
    Serial.println("[SIM] course JSON invalid or incomplete");
    clearCourses();
    return false;
  }
  return true;
}

} // namespace sim
