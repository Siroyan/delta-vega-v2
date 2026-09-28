#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace vega {

using Millis = uint64_t;
constexpr size_t kLapCount = 7;
struct GeoPoint {
  double latitude = 0;
  double longitude = 0;
};
struct Settings {
  uint32_t version = 1;
  uint32_t total_target_s = 42 * 60;
  std::array<uint32_t, kLapCount> lap_target_s{{360, 360, 360, 360, 360, 360, 360}};
  GeoPoint start{36.530654, 140.227998};
  GeoPoint timing{36.532766, 140.226269};
  GeoPoint goal{36.534443, 140.225411};
  double wheel_circumference_m = 1.03;
  uint32_t pulses_per_revolution = 1;
  uint32_t ecu_ready_ms = 1000;
  uint32_t ignition_pulse_ms = 1000;
  bool power_active_high = true;
  uint32_t pulse_debounce_us = 3000;
  uint32_t speed_zero_ms = 3000;
  uint32_t gps_stale_ms = 3000;
  double course_corridor_m = 60;
  double max_gps_step_m = 80;
  double min_lap_progress_m = 600;
  uint32_t min_lap_ms = 60000;
  uint32_t lap_duplicate_ms = 10000;
};
bool validSettings(const Settings &s);
enum class RacePhase : uint8_t { Waiting, Measuring, Finished };
enum class EnginePhase : uint8_t { Off, Preparing, Ready, Pulsing, Issued };
enum class EndReason : uint8_t { Finished, Cancelled };
enum class Event : uint8_t {
  Started,
  Cancelled,
  Finished,
  ManualLap,
  GpsLap,
  PowerOn,
  PowerOff,
  Ignition,
  SettingsSaved,
  ManualFinish
};
struct WheelInput {
  uint64_t pulses = 0;
  uint64_t last_pulse_us = 0;
  uint64_t previous_pulse_us = 0;
};
struct GpsFix {
  GeoPoint position{};
  Millis received_ms = 0;
  double course_deg = 0;
  double speed_kmh = 0;
  double hdop = 0;
  uint8_t satellites = 0;
  bool valid = false;
  bool has_heading = false;
};
struct WheelReading {
  double speed_kmh = 0;
  bool valid = false;
  bool pulse_recent = false;
  uint64_t pulses = 0;
};
struct RaceReading {
  RacePhase phase = RacePhase::Waiting;
  uint8_t lap = 0;
  Millis total_ms = 0;
  Millis lap_ms = 0;
  double distance_m = 0;
  double average_kmh = 0;
  uint32_t session = 0;
  bool lap_approximate = false;
};
struct MapPosition {
  double x = 0;
  double y = 0;
  double s_m = 0;
  double lateral_m = 0;
  bool on_course = false;
};
struct Snapshot {
  Millis now_ms = 0;
  Settings settings{};
  RaceReading race{};
  EnginePhase engine = EnginePhase::Off;
  WheelReading wheel{};
  GpsFix gps{};
  MapPosition map{};
  bool gps_fresh = false;
  bool gps_seen = false;
  bool output_error = false;
  bool settings_error = false;
  uint32_t settings_attempt = 0;
  bool settings_accepted = false;
};
inline bool validGeo(GeoPoint p) {
  return std::isfinite(p.latitude) && std::isfinite(p.longitude) && std::abs(p.latitude) <= 90 &&
         std::abs(p.longitude) <= 180;
}
}  // namespace vega
