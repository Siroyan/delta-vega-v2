#include "settings_form.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace vega {
const char *settingTitle(size_t field) {
  static const char *titles[] = {"TOTAL TARGET", "LAP 1 TARGET", "LAP 2 TARGET", "LAP 3 TARGET",
                                 "LAP 4 TARGET", "LAP 5 TARGET", "LAP 6 TARGET", "LAP 7 TARGET",
                                 "START LAT",    "START LON",    "TIMING LAT",   "TIMING LON",
                                 "GOAL LAT",     "GOAL LON"};
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
  } else {
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
  } else {
    auto point = field < 10 ? s.start : field < 12 ? s.timing : s.goal;
    std::snprintf(out, cap, "%.8f", field % 2 == 0 ? point.latitude : point.longitude);
  }
}
}  // namespace vega
