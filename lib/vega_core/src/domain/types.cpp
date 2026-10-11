#include "types.h"

namespace vega {
bool validSettings(const Settings &s) {
  if (s.version != 4 || (s.gps_source != GpsSource::M5Bus && s.gps_source != GpsSource::PortA) ||
      s.display_brightness < kMinDisplayBrightness ||
      s.display_brightness > kMaxDisplayBrightness ||
      s.speed_average_intervals < 1 ||
      s.speed_average_intervals > kMaxSpeedAverageIntervals ||
      s.total_target_s == 0 || s.total_target_s > 86400 || !validGeo(s.start) ||
      !validGeo(s.timing) || !validGeo(s.goal) || !std::isfinite(s.wheel_circumference_m) ||
      s.wheel_circumference_m < 0.1 || s.wheel_circumference_m > 10 ||
      s.pulses_per_revolution == 0 || s.pulses_per_revolution > 100 || s.ecu_ready_ms > 60000 ||
      s.ignition_pulse_ms == 0 || s.ignition_pulse_ms > 10000 || s.pulse_debounce_us < 100 ||
      s.pulse_debounce_us > 100000 || s.gps_stale_ms < 500 || s.gps_stale_ms > 30000 ||
      s.speed_zero_ms < 500 || s.speed_zero_ms > 30000 || !std::isfinite(s.course_corridor_m) ||
      s.course_corridor_m < 1 || s.course_corridor_m > 200 || !std::isfinite(s.max_gps_step_m) ||
      s.max_gps_step_m < 1 || s.max_gps_step_m > 200 || !std::isfinite(s.min_lap_progress_m) ||
      s.min_lap_progress_m < 0 || s.min_lap_progress_m > 5000 || s.min_lap_ms > 3600000 ||
      s.lap_duplicate_ms < 500 || s.lap_duplicate_ms > 60000)
    return false;
  for (auto t : s.lap_target_s)
    if (t == 0 || t > 86400) return false;
  return true;
}
}  // namespace vega
