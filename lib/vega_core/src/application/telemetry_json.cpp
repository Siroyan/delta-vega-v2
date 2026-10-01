#include "telemetry_json.h"

#include <cstdio>
#include <cstring>

namespace vega {
namespace {
bool escape(const char *in, char *out, size_t cap) {
  size_t used = 0;
  for (; *in; ++in) {
    unsigned char c = *in;
    if (c < 32) {
      if (used + 6 >= cap) return false;
      std::snprintf(out + used, cap - used, "\\u%04x", c);
      used += 6;
    } else {
      if (used + 2 >= cap) return false;
      if (c == '"' || c == '\\') out[used++] = '\\';
      out[used++] = c;
    }
  }
  out[used] = 0;
  return true;
}
void value(double number, bool valid, char *out, size_t cap, int digits) {
  if (valid && std::isfinite(number))
    std::snprintf(out, cap, "%.*f", digits, number);
  else
    std::snprintf(out, cap, "null");
}
}  // namespace
size_t telemetryJson(const Snapshot &s, char *out, size_t cap, const char *machine,
                     const char *memo) {
  char id[128], note[256], speed[32], average[32], latitude[32], longitude[32];
  if (!out || !cap || !machine || !memo || !escape(machine, id, sizeof(id)) ||
      !escape(memo, note, sizeof(note)))
    return 0;
  value(s.wheel.speed_kmh, s.wheel.valid, speed, sizeof(speed), 3);
  value(s.race.average_kmh, s.wheel.pulses > 0 && s.race.phase != RacePhase::Waiting, average,
        sizeof(average), 3);
  value(s.gps.position.latitude, s.gps_fresh, latitude, sizeof(latitude), 8);
  value(s.gps.position.longitude, s.gps_fresh, longitude, sizeof(longitude), 8);
  int length = std::snprintf(out, cap,
                             "{\"speed\":%s,\"average_speed\":%s,\"lap_number\":%u,\"total_time_"
                             "ms\":%llu,\"lap_time_ms\":%llu,\"latitude\":%s,\"longitude\":%s,"
                             "\"timestamp_ms\":%llu,\"machine_id\":\"%s\",\"memo\":\"%s\"}",
                             speed, average, s.race.lap, (unsigned long long)s.race.total_ms,
                             (unsigned long long)s.race.lap_ms, latitude, longitude,
                             (unsigned long long)s.now_ms, id, note);
  return length > 0 && size_t(length) < cap ? size_t(length) : 0;
}
size_t sessionSampleJson(const Snapshot &s, char *out, size_t cap, const char *machine,
                         const char *memo) {
  const size_t base = telemetryJson(s, out, cap, machine, memo);
  if (!base || out[base - 1] != '}') return 0;
  char speed[32], hdop[32], satellites[16], quality[16], utc[24], age[24], quality_age[24];
  value(s.gps.speed_kmh, s.gps_seen && s.gps.speed_valid, speed, sizeof(speed), 3);
  value(s.gps.hdop, s.gps_seen && s.gps.quality_valid, hdop, sizeof(hdop), 2);
  if (s.gps_seen && s.gps.quality_valid) {
    std::snprintf(satellites, sizeof(satellites), "%u", s.gps.satellites);
    std::snprintf(quality, sizeof(quality), "%u", s.gps.gga_fix_quality);
  } else {
    std::snprintf(satellites, sizeof(satellites), "null");
    std::snprintf(quality, sizeof(quality), "null");
  }
  if (s.gps_seen && s.gps.utc_valid)
    std::snprintf(utc, sizeof(utc), "%u", s.gps.utc_ms_of_day);
  else
    std::snprintf(utc, sizeof(utc), "null");
  if (s.gps_seen && s.now_ms >= s.gps.received_ms)
    std::snprintf(age, sizeof(age), "%llu", (unsigned long long)(s.now_ms - s.gps.received_ms));
  else
    std::snprintf(age, sizeof(age), "null");
  if (s.gps_seen && s.gps.quality_valid && s.now_ms >= s.gps.quality_received_ms)
    std::snprintf(quality_age, sizeof(quality_age), "%llu",
                  (unsigned long long)(s.now_ms - s.gps.quality_received_ms));
  else
    std::snprintf(quality_age, sizeof(quality_age), "null");
  const int length = std::snprintf(
      out + base - 1, cap - base + 1,
      ",\"gps_seen\":%s,\"gps_fix_valid\":%s,\"gps_fresh\":%s,"
      "\"gps_speed_kmh\":%s,\"gps_satellites\":%s,\"gps_hdop\":%s,"
      "\"gps_gga_fix_quality\":%s,\"gps_utc_ms_of_day\":%s,"
      "\"gps_age_ms\":%s,\"gps_quality_age_ms\":%s}",
      s.gps_seen ? "true" : "false", s.gps_seen && s.gps.valid ? "true" : "false",
      s.gps_fresh ? "true" : "false", speed, satellites, hdop, quality, utc, age,
      quality_age);
  if (length <= 0 || size_t(length) >= cap - base + 1) return 0;
  return base - 1 + size_t(length);
}
}  // namespace vega
