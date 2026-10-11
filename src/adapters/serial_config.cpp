#include "serial_config.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace tab5 {

bool serialConfigCommand(const char *key, const char *value,
                         const vega::Settings &current, vega::Command &out) {
  char *end = nullptr;
  const double number = std::strtod(value, &end);
  if (end == value || *end || !std::isfinite(number) || number < 0) return false;

  vega::Command command{};
  command.kind = vega::CommandKind::Configure;
  command.scope = vega::SettingsScope::General;
  command.settings = current;

  // 実数項目と整数項目で許容値が異なる。コース固有の項目は保存先も切り替える。
  if (!std::strcmp(key, "power_active_high")) {
    if (number != 0 && number != 1) return false;
    command.settings.power_active_high = number == 1;
  } else if (!std::strcmp(key, "wheel_circumference_m")) {
    command.settings.wheel_circumference_m = number;
  } else if (!std::strcmp(key, "course_corridor_m")) {
    command.scope = vega::SettingsScope::Course;
    command.settings.course_corridor_m = number;
  } else if (!std::strcmp(key, "max_gps_step_m")) {
    command.settings.max_gps_step_m = number;
  } else if (!std::strcmp(key, "min_lap_progress_m")) {
    command.scope = vega::SettingsScope::Course;
    command.settings.min_lap_progress_m = number;
  } else {
    if (number > UINT32_MAX || number != std::floor(number)) return false;
    if (!std::strcmp(key, "ecu_ready_ms"))
      command.settings.ecu_ready_ms = number;
    else if (!std::strcmp(key, "ignition_pulse_ms"))
      command.settings.ignition_pulse_ms = number;
    else if (!std::strcmp(key, "pulse_debounce_us"))
      command.settings.pulse_debounce_us = number;
    else if (!std::strcmp(key, "speed_average_intervals"))
      command.settings.speed_average_intervals = number;
    else if (!std::strcmp(key, "speed_zero_ms"))
      command.settings.speed_zero_ms = number;
    else if (!std::strcmp(key, "gps_stale_ms"))
      command.settings.gps_stale_ms = number;
    else if (!std::strcmp(key, "min_lap_ms")) {
      command.scope = vega::SettingsScope::Course;
      command.settings.min_lap_ms = number;
    } else if (!std::strcmp(key, "lap_duplicate_ms")) {
      command.scope = vega::SettingsScope::Course;
      command.settings.lap_duplicate_ms = number;
    } else if (!std::strcmp(key, "pulses_per_revolution"))
      command.settings.pulses_per_revolution = number;
    else
      return false;
  }

  if (!vega::validSettings(command.settings)) return false;
  out = command;
  return true;
}

}  // namespace tab5
