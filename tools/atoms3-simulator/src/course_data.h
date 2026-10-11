#pragma once

#include <stdint.h>

namespace sim {

constexpr uint8_t kMaxCourses = 8;
constexpr uint8_t kMaxLaps = 100;

struct Point { float east_m, north_m, s_m; };
struct Route { Point *points = nullptr; uint16_t count = 0; float length_m = 0; };
struct Course {
  char id[41]{};
  char name[48]{};
  char short_name[24]{};
  uint8_t lap_count = 0;
  double origin_lat = 0, origin_lon = 0;
  double east_per_lon = 0, north_per_lat = 0;
  Route first, regular, final;
};

extern Course courses[kMaxCourses];
extern uint8_t course_count;

// Read the same schema-2 course.json bytes used by Tab5, from LittleFS.
bool loadCoursesFromFs();

} // namespace sim
