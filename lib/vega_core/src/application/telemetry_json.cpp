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
}  // namespace vega
