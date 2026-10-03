#include "settings_codec.h"

#include <cstring>
#include <type_traits>

namespace vega {
namespace {
// Exact on-device layout of the version-1 blob. Keep this frozen for upgrades.
struct SettingsV1 {
  uint32_t version;
  uint32_t total_target_s;
  std::array<uint32_t, kLapCount> lap_target_s;
  GeoPoint start, timing, goal;
  double wheel_circumference_m;
  uint32_t pulses_per_revolution;
  uint32_t ecu_ready_ms;
  uint32_t ignition_pulse_ms;
  bool power_active_high;
  uint32_t pulse_debounce_us;
  uint32_t speed_zero_ms;
  uint32_t gps_stale_ms;
  double course_corridor_m;
  double max_gps_step_m;
  double min_lap_progress_m;
  uint32_t min_lap_ms;
  uint32_t lap_duplicate_ms;
};
struct SettingsV2 {
  SettingsV1 previous;
  GpsSource gps_source;
};
static_assert(std::is_trivially_copyable<SettingsV1>::value, "NVS v1 must be a plain blob");
static_assert(sizeof(SettingsV1) == 160, "NVS v1 layout changed");
static_assert(offsetof(Settings, gps_source) == sizeof(SettingsV1),
              "NVS v1 prefix no longer matches Settings");
static_assert(sizeof(SettingsV2) == 168, "NVS v2 layout changed");
static_assert(offsetof(Settings, display_brightness) ==
                  offsetof(SettingsV2, gps_source) + sizeof(GpsSource),
              "NVS v2 prefix no longer matches Settings");
static_assert(sizeof(Settings) >= sizeof(SettingsV2), "NVS v3 must contain the v2 fields");
static_assert(offsetof(Settings, speed_average_intervals) == 168,
              "NVS v3 prefix no longer matches Settings");
}  // namespace

bool decodeSettingsBlob(const void *blob, size_t size, Settings &out) {
  if (!blob || size < sizeof(uint32_t)) return false;
  uint32_t version = 0;
  std::memcpy(&version, blob, sizeof(version));
  Settings candidate;
  if (version == 4 && size == sizeof(Settings)) {
    std::memcpy(&candidate, blob, size);
  } else if (version == 3 && size == offsetof(Settings, speed_average_intervals)) {
    std::memcpy(&candidate, blob, size);
    candidate.version = 4;
  } else if (version == 2 && size == sizeof(SettingsV2)) {
    // v2's tail padding may have the same blob size as v3. Inspect the version
    // before copying, and leave the new brightness field at its safe default.
    std::memcpy(&candidate, blob, offsetof(Settings, display_brightness));
    candidate.version = 4;
  } else if (version == 1 && size == sizeof(SettingsV1)) {
    SettingsV1 old;
    std::memcpy(&old, blob, size);
    candidate.total_target_s = old.total_target_s;
    candidate.lap_target_s = old.lap_target_s;
    candidate.start = old.start;
    candidate.timing = old.timing;
    candidate.goal = old.goal;
    candidate.wheel_circumference_m = old.wheel_circumference_m;
    candidate.pulses_per_revolution = old.pulses_per_revolution;
    candidate.ecu_ready_ms = old.ecu_ready_ms;
    candidate.ignition_pulse_ms = old.ignition_pulse_ms;
    candidate.power_active_high = old.power_active_high;
    candidate.pulse_debounce_us = old.pulse_debounce_us;
    candidate.speed_zero_ms = old.speed_zero_ms;
    candidate.gps_stale_ms = old.gps_stale_ms;
    candidate.course_corridor_m = old.course_corridor_m;
    candidate.max_gps_step_m = old.max_gps_step_m;
    candidate.min_lap_progress_m = old.min_lap_progress_m;
    candidate.min_lap_ms = old.min_lap_ms;
    candidate.lap_duplicate_ms = old.lap_duplicate_ms;
  } else {
    return false;
  }
  if (!validSettings(candidate)) return false;
  out = candidate;
  return true;
}
}  // namespace vega
