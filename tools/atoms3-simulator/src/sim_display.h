#pragma once

#include <stdint.h>

namespace sim_display {

enum class Mode : uint8_t { Idle, Running, Paused, Finished, Stopped };

struct Snapshot {
  const char *course_name;
  Mode mode;
  uint8_t lap;
  uint8_t lap_count;
  float speed_kmh;
  float distance_m;
  float route_fraction;
  uint64_t pulses;
  uint32_t missed_pulses;
  const char *gps_status;
};

void begin();
void update(const Snapshot &snapshot, uint32_t now_ms);

} // namespace sim_display
