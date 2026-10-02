#include "settings_scopes.h"

#include <type_traits>

namespace vega {
static_assert(std::is_trivially_copyable<GeneralSettings>::value,
              "GeneralSettings must remain an NVS blob");
static_assert(std::is_trivially_copyable<CourseSettings>::value,
              "CourseSettings must remain an NVS blob");
GeneralSettings generalSettings(const Settings &s) {
  GeneralSettings g;
  g.wheel_circumference_m = s.wheel_circumference_m;
  g.pulses_per_revolution = s.pulses_per_revolution;
  g.ecu_ready_ms = s.ecu_ready_ms;
  g.ignition_pulse_ms = s.ignition_pulse_ms;
  g.power_active_high = s.power_active_high;
  g.pulse_debounce_us = s.pulse_debounce_us;
  g.speed_zero_ms = s.speed_zero_ms;
  g.gps_stale_ms = s.gps_stale_ms;
  g.max_gps_step_m = s.max_gps_step_m;
  g.gps_source = s.gps_source;
  g.display_brightness = s.display_brightness;
  return g;
}
CourseSettings courseSettings(const Settings &s) {
  CourseSettings c;
  c.total_target_s = s.total_target_s;
  c.lap_target_s = s.lap_target_s;
  c.start = s.start;
  c.timing = s.timing;
  c.goal = s.goal;
  c.course_corridor_m = s.course_corridor_m;
  c.min_lap_progress_m = s.min_lap_progress_m;
  c.min_lap_ms = s.min_lap_ms;
  c.lap_duplicate_ms = s.lap_duplicate_ms;
  return c;
}
void applyGeneral(Settings &s, const GeneralSettings &g) {
  s.wheel_circumference_m = g.wheel_circumference_m;
  s.pulses_per_revolution = g.pulses_per_revolution;
  s.ecu_ready_ms = g.ecu_ready_ms;
  s.ignition_pulse_ms = g.ignition_pulse_ms;
  s.power_active_high = g.power_active_high;
  s.pulse_debounce_us = g.pulse_debounce_us;
  s.speed_zero_ms = g.speed_zero_ms;
  s.gps_stale_ms = g.gps_stale_ms;
  s.max_gps_step_m = g.max_gps_step_m;
  s.gps_source = g.gps_source;
  s.display_brightness = g.display_brightness;
}
void applyCourse(Settings &s, const CourseSettings &c) {
  s.total_target_s = c.total_target_s;
  s.lap_target_s = c.lap_target_s;
  s.start = c.start;
  s.timing = c.timing;
  s.goal = c.goal;
  s.course_corridor_m = c.course_corridor_m;
  s.min_lap_progress_m = c.min_lap_progress_m;
  s.min_lap_ms = c.min_lap_ms;
  s.lap_duplicate_ms = c.lap_duplicate_ms;
}
bool validGeneralSettings(const GeneralSettings &g) {
  if (g.version != 1) return false;
  Settings s;
  applyGeneral(s, g);
  return validSettings(s);
}
bool validCourseSettings(const CourseSettings &c) {
  if (c.version != 1) return false;
  Settings s;
  applyCourse(s, c);
  return validSettings(s);
}
}  // namespace vega
