#pragma once

#include "types.h"

namespace vega {
enum class SettingsScope : uint8_t { Course, General };

// Independent NVS records. Settings remains the combined runtime snapshot and
// the frozen legacy blob layout used only for migration.
struct GeneralSettings {
  uint32_t version = 2;
  double wheel_circumference_m = 1.03;
  uint32_t pulses_per_revolution = 1;
  uint32_t ecu_ready_ms = 1000;
  uint32_t ignition_pulse_ms = 1000;
  bool power_active_high = true;
  uint32_t pulse_debounce_us = 3000;
  uint32_t speed_zero_ms = 3000;
  uint32_t gps_stale_ms = 3000;
  double max_gps_step_m = 80;
  GpsSource gps_source = GpsSource::M5Bus;
  uint32_t display_brightness = kDefaultDisplayBrightness;
  uint32_t speed_average_intervals = 3;
};

struct CourseSettings {
  uint32_t version = 1;
  uint32_t total_target_s = 42 * 60;
  std::array<uint32_t, kLapCount> lap_target_s{{360, 360, 360, 360, 360, 360, 360}};
  GeoPoint start{}, timing{}, goal{};
  double course_corridor_m = 60;
  double min_lap_progress_m = 600;
  uint32_t min_lap_ms = 60000;
  uint32_t lap_duplicate_ms = 10000;
};

GeneralSettings generalSettings(const Settings &settings);
CourseSettings courseSettings(const Settings &settings);
void applyGeneral(Settings &settings, const GeneralSettings &general);
void applyCourse(Settings &settings, const CourseSettings &course);
bool validGeneralSettings(const GeneralSettings &general);
// Decode both the current device-wide NVS blob and the version-1 layout.
bool decodeGeneralSettingsBlob(const void *blob, size_t size, GeneralSettings &out);
bool validCourseSettings(const CourseSettings &course);
}  // namespace vega
