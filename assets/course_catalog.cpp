#include "course_catalog.h"

#include <cstring>

#include "motegi_oval_full/course_data.h"
#include "tamagawagakuen_station_loop/course_data.h"
#include "tobitakyu_hospital_loop/course_data.h"

extern "C" {
extern const lv_image_dsc_t img_motegi_background_480;
extern const lv_image_dsc_t img_tamagawagakuen_course_480;
extern const lv_image_dsc_t img_tobitakyu_course_480;
}

namespace tab5 {
namespace {
vega::Settings motegiDefaults() {
  vega::Settings s;
  s.start = {36.530654, 140.227998};
  s.timing = {36.532766, 140.226269};
  s.goal = {36.534443, 140.225411};
  return s;
}
vega::Settings tamagawaDefaults() {
  vega::Settings s;
  s.start = {35.564980, 139.463466};
  s.timing = {35.5647900, 139.4640418};
  s.goal = {35.5633809, 139.4629657};
  s.course_corridor_m = 30;
  s.total_target_s = 40 * 60;
  s.lap_target_s.fill(10 * 60);
  return s;
}
vega::Settings tobitakyuDefaults() {
  vega::Settings s;
  s.start = {35.666947, 139.518721};
  s.timing = {35.6665666, 139.5186953};
  s.goal = {35.6669552, 139.5219829};
  s.course_corridor_m = 30;
  s.total_target_s = 80 * 60;
  s.lap_target_s.fill(20 * 60);
  return s;
}
// Order determines the first-install choice. It preserves the previous
// development firmware's default without treating any course as a base course.
const CourseAsset courses[] = {
    {"TAMAGAWA GAKUEN", "TAMAGAWA", &asset_tamagawagakuen_station_loop::course_data,
     &img_tamagawagakuen_course_480, "vega-test4", "vega-test", 70 * 60, tamagawaDefaults, true},
    {"TOBITAKYU", "TOBITAKYU", &asset_tobitakyu_hospital_loop::course_data,
     &img_tobitakyu_course_480, "vega-tobi4", "vega-tobi2", 140 * 60, tobitakyuDefaults, true},
    {"MOTEGI OVAL", "MOTEGI", &asset_motegi_oval_full::course_data,
     &img_motegi_background_480, "vega", nullptr, 0, motegiDefaults, false},
};
}  // namespace

size_t courseCount() { return sizeof(courses) / sizeof(courses[0]); }
const CourseAsset &courseAsset(size_t index) {
  return courses[index < courseCount() ? index : 0];
}
size_t courseIndex(const char *id) {
  if (id) for (size_t i = 0; i < courseCount(); ++i)
    if (std::strcmp(id, courses[i].data->id) == 0) return i;
  return 0;
}
}  // namespace tab5
