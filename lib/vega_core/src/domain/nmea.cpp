#include "nmea.h"

#include <cstdlib>
#include <cstring>

namespace vega {
namespace {
int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
bool number(const char *p, double &value) {
  if (!p || !*p) return false;
  char *end = nullptr;
  value = std::strtod(p, &end);
  return end != p && *end == '\0' && std::isfinite(value);
}
bool coordinate(const char *text, const char *hemisphere, bool latitude, double &out) {
  double value;
  if (!number(text, value) || value < 0 || std::strlen(hemisphere) != 1) return false;
  double degree = std::floor(value / 100), minute = value - degree * 100;
  if (minute >= 60) return false;
  out = degree + minute / 60;
  char h = hemisphere[0];
  if (latitude ? (h != 'N' && h != 'S') : (h != 'E' && h != 'W')) return false;
  if (h == 'S' || h == 'W') out = -out;
  return std::abs(out) <= (latitude ? 90 : 180);
}
bool utcTime(const char *text, uint32_t &out) {
  if (!text || std::strlen(text) < 6) return false;
  for (int i = 0; i < 6; ++i)
    if (text[i] < '0' || text[i] > '9') return false;
  const int hour = (text[0] - '0') * 10 + text[1] - '0';
  const int minute = (text[2] - '0') * 10 + text[3] - '0';
  const int second = (text[4] - '0') * 10 + text[5] - '0';
  if (hour > 23 || minute > 59 || second > 59) return false;
  uint32_t fraction = 0;
  if (text[6]) {
    if (text[6] != '.' || !text[7]) return false;
    int digits = 0;
    for (const char *p = text + 7; *p; ++p, ++digits) {
      if (*p < '0' || *p > '9') return false;
      if (digits < 3) fraction = fraction * 10 + (*p - '0');
    }
    for (; digits < 3; ++digits) fraction *= 10;
  }
  out = uint32_t((hour * 3600 + minute * 60 + second) * 1000) + fraction;
  return true;
}
}  // namespace
bool NmeaParser::feed(char c, Millis now, GpsFix &fix) {
  if (c == '$') {
    length_ = 0;
    collecting_ = true;
    return false;
  }
  if (!collecting_ || c == '\r') return false;
  if (c == '\n') {
    line_[length_] = '\0';
    collecting_ = false;
    return parse(now, fix);
  }
  if (length_ + 1 >= sizeof(line_)) {
    collecting_ = false;
    ++rejected_;
    return false;
  }
  line_[length_++] = c;
  return false;
}
bool NmeaParser::parse(Millis now, GpsFix &fix) {
  auto *asterisk = std::strchr(line_, '*');
  if (!asterisk || std::strlen(asterisk) != 3 || hex(asterisk[1]) < 0 || hex(asterisk[2]) < 0) {
    ++rejected_;
    return false;
  }
  uint8_t checksum = 0;
  for (auto *p = line_; p < asterisk; ++p) checksum ^= *p;
  if (checksum != (hex(asterisk[1]) * 16 + hex(asterisk[2]))) {
    ++rejected_;
    return false;
  }
  *asterisk = '\0';
  char *fields[24]{};
  size_t count = 1;
  fields[0] = line_;
  for (auto *p = line_; *p && count < 24; ++p)
    if (*p == ',') {
      *p = '\0';
      fields[count++] = p + 1;
    }
  if (std::strlen(fields[0]) != 5) return false;
  auto *kind = fields[0] + 2;
  if (std::strcmp(kind, "GGA") == 0 && count >= 9) {
    double quality, sats, hdop;
    if (number(fields[6], quality) && quality >= 0 && quality <= 8 &&
        number(fields[7], sats) && sats >= 0 && sats <= 99 &&
        number(fields[8], hdop) && hdop >= 0 && hdop <= 99) {
      gga_fix_quality_ = uint8_t(quality);
      satellites_ = uint8_t(sats);
      hdop_ = hdop;
      quality_received_ms_ = now;
      quality_valid_ = true;
    }
    return false;
  }
  if (std::strcmp(kind, "RMC") != 0 || count < 10) return false;
  fix = {};
  fix.received_ms = now;
  fix.satellites = satellites_;
  fix.gga_fix_quality = gga_fix_quality_;
  fix.hdop = hdop_;
  fix.quality_received_ms = quality_received_ms_;
  fix.quality_valid = quality_valid_;
  fix.utc_valid = utcTime(fields[1], fix.utc_ms_of_day);
  if (std::strcmp(fields[2], "A") != 0) return true;
  fix.valid = coordinate(fields[3], fields[4], true, fix.position.latitude) &&
              coordinate(fields[5], fields[6], false, fix.position.longitude);
  double speed = 0, heading = 0;
  fix.speed_valid = number(fields[7], speed) && speed >= 0 && speed < 300;
  if (fix.speed_valid) fix.speed_kmh = speed * 1.852;
  fix.has_heading =
      number(fields[8], heading) && heading >= 0 && heading < 360 && fix.speed_kmh >= 3;
  if (fix.has_heading) fix.course_deg = heading;
  return true;
}
}  // namespace vega
