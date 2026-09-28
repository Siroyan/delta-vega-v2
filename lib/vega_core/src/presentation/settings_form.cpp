#include "settings_form.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
namespace vega {
namespace {
bool parseUnsigned(const char *text, uint32_t &value) {
  if (!text || !*text) return false;
  uint64_t number = 0;
  for (const char *p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    number = number * 10 + (*p - '0');
    if (number > std::numeric_limits<uint32_t>::max()) return false;
  }
  value = static_cast<uint32_t>(number);
  return true;
}
bool parseDecimal(const char *text, double &value) {
  if (!text || !*text) return false;
  bool digit = false, dot = false;
  for (const char *p = text; *p; ++p) {
    if (*p >= '0' && *p <= '9')
      digit = true;
    else if (*p == '.' && !dot)
      dot = true;
    else
      return false;
  }
  if (!digit) return false;
  char *end = nullptr;
  value = std::strtod(text, &end);
  return end && !*end && std::isfinite(value);
}
}  // namespace
const char *settingTitle(size_t field) {
  static const char *titles[] = {"TOTAL TARGET", "LAP 1 TARGET", "LAP 2 TARGET", "LAP 3 TARGET",
                                 "LAP 4 TARGET", "LAP 5 TARGET", "LAP 6 TARGET", "LAP 7 TARGET",
                                 "START LAT",    "START LON",    "TIMING LAT",   "TIMING LON",
                                 "GOAL LAT",     "GOAL LON",     "WHEEL CIRC. (m)",
                                 "PULSES / REV", "ECU READY (ms)", "IGNITION PULSE (ms)",
                                 "POWER HIGH (0/1)", "DEBOUNCE (us)", "ZERO SPEED (ms)",
                                 "GPS STALE (ms)", "COURSE CORRIDOR (m)", "MAX GPS STEP (m)",
                                 "MIN LAP PROGRESS (m)", "MIN LAP TIME (ms)",
                                 "LAP DUPLICATE (ms)"};
  static_assert(sizeof(titles) / sizeof(titles[0]) == kSettingsFieldCount);
  return field < kSettingsFieldCount ? titles[field] : "";
}
bool editSetting(Settings &s, size_t field, const char *text) {
  if (!text || !text[0] || field >= kSettingsFieldCount) return false;
  Settings candidate = s;
  if (field < 8) {
    // Explicit MM:SS / H:MM:SS, bounded digits and components; never silently truncate.
    unsigned parts[3]{}, count = 0, digits = 0;
    for (const char *p = text;; ++p) {
      if (*p >= '0' && *p <= '9') {
        if (++digits > 5) return false;
        parts[count] = parts[count] * 10 + (*p - '0');
      } else if (*p == ':' || *p == '\0') {
        if (!digits) return false;
        ++count;
        digits = 0;
        if (*p == '\0') break;
        if (count >= 3) return false;
      } else
        return false;
    }
    if (count < 2 || parts[count - 1] >= 60 || (count == 3 && parts[1] >= 60)) return false;
    uint64_t seconds = count == 2 ? uint64_t(parts[0]) * 60 + parts[1]
                                  : uint64_t(parts[0]) * 3600 + parts[1] * 60 + parts[2];
    if (!seconds || seconds > 86400) return false;
    if (field == 0)
      candidate.total_target_s = seconds;
    else
      candidate.lap_target_s[field - 1] = seconds;
  } else if (field < 14) {
    // Reject whitespace, hexadecimal/exponent syntax and trailing text.
    bool digit = false;
    for (const char *p = text; *p; ++p) {
      if (*p >= '0' && *p <= '9')
        digit = true;
      else if (*p != '.' && !(*p == '-' && p == text))
        return false;
    }
    if (!digit) return false;
    char *end = nullptr;
    double number = std::strtod(text, &end);
    if (!end || *end || !std::isfinite(number)) return false;
    GeoPoint *point = field < 10   ? &candidate.start
                      : field < 12 ? &candidate.timing
                                   : &candidate.goal;
    if (field % 2 == 0)
      point->latitude = number;
    else
      point->longitude = number;
  } else if (field == 18) {
    if (std::strcmp(text, "0") && std::strcmp(text, "1")) return false;
    candidate.power_active_high = text[0] == '1';
  } else if (field == 14 || (field >= 22 && field <= 24)) {
    double number;
    if (!parseDecimal(text, number)) return false;
    switch (field) {
      case 14: candidate.wheel_circumference_m = number; break;
      case 22: candidate.course_corridor_m = number; break;
      case 23: candidate.max_gps_step_m = number; break;
      case 24: candidate.min_lap_progress_m = number; break;
    }
  } else {
    uint32_t number;
    if (!parseUnsigned(text, number)) return false;
    switch (field) {
      case 15: candidate.pulses_per_revolution = number; break;
      case 16: candidate.ecu_ready_ms = number; break;
      case 17: candidate.ignition_pulse_ms = number; break;
      case 19: candidate.pulse_debounce_us = number; break;
      case 20: candidate.speed_zero_ms = number; break;
      case 21: candidate.gps_stale_ms = number; break;
      case 25: candidate.min_lap_ms = number; break;
      case 26: candidate.lap_duplicate_ms = number; break;
      default: return false;
    }
  }
  if (!validSettings(candidate)) return false;
  s = candidate;
  return true;
}
void settingText(const Settings &s, size_t field, char *out, size_t cap) {
  if (field >= kSettingsFieldCount) {
    if (cap) out[0] = 0;
    return;
  }
  if (field < 8) {
    auto seconds = field == 0 ? s.total_target_s : s.lap_target_s[field - 1];
    if (seconds >= 3600)
      std::snprintf(out, cap, "%lu:%02lu:%02lu", static_cast<unsigned long>(seconds / 3600),
                    static_cast<unsigned long>(seconds / 60 % 60),
                    static_cast<unsigned long>(seconds % 60));
    else
      std::snprintf(out, cap, "%02lu:%02lu", static_cast<unsigned long>(seconds / 60),
                    static_cast<unsigned long>(seconds % 60));
  } else if (field < 14) {
    auto point = field < 10 ? s.start : field < 12 ? s.timing : s.goal;
    std::snprintf(out, cap, "%.8f", field % 2 == 0 ? point.latitude : point.longitude);
  } else if (field == 14 || (field >= 22 && field <= 24)) {
    double value = field == 14   ? s.wheel_circumference_m
                   : field == 22 ? s.course_corridor_m
                   : field == 23 ? s.max_gps_step_m
                                 : s.min_lap_progress_m;
    std::snprintf(out, cap, "%.8f", value);
  } else {
    uint32_t value = field == 15   ? s.pulses_per_revolution
                     : field == 16 ? s.ecu_ready_ms
                     : field == 17 ? s.ignition_pulse_ms
                     : field == 18 ? static_cast<uint32_t>(s.power_active_high)
                     : field == 19 ? s.pulse_debounce_us
                     : field == 20 ? s.speed_zero_ms
                     : field == 21 ? s.gps_stale_ms
                     : field == 25 ? s.min_lap_ms
                                   : s.lap_duplicate_ms;
    std::snprintf(out, cap, "%lu", static_cast<unsigned long>(value));
  }
}
}  // namespace vega
