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
    double sats, hdop;
    if (number(fields[7], sats) && sats >= 0 && sats <= 99) satellites_ = uint8_t(sats);
    if (number(fields[8], hdop) && hdop >= 0 && hdop <= 99) hdop_ = hdop;
    return false;
  }
  if (std::strcmp(kind, "RMC") != 0 || count < 10) return false;
  fix = {};
  fix.received_ms = now;
  fix.satellites = satellites_;
  fix.hdop = hdop_;
  if (std::strcmp(fields[2], "A") != 0) return true;
  fix.valid = coordinate(fields[3], fields[4], true, fix.position.latitude) &&
              coordinate(fields[5], fields[6], false, fix.position.longitude);
  double speed = 0, heading = 0;
  if (number(fields[7], speed) && speed >= 0 && speed < 300) fix.speed_kmh = speed * 1.852;
  fix.has_heading =
      number(fields[8], heading) && heading >= 0 && heading < 360 && fix.speed_kmh >= 3;
  if (fix.has_heading) fix.course_deg = heading;
  return true;
}
}  // namespace vega
