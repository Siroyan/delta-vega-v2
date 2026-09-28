#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "course.h"

namespace vega {

constexpr size_t kMaxStrategyRunsPerLap = 3;
constexpr size_t kMaxStrategyFileBytes = 4096;

struct StrategyRun {
  double on_s_m = 0;
  double off_s_m = 0;
};
struct StrategyLap {
  CourseRoute route = CourseRoute::Regular;
  double route_length_m = 0;
  uint8_t run_count = 0;
  std::array<StrategyRun, kMaxStrategyRunsPerLap> runs{};
};
struct Strategy {
  char course_id[48]{};
  char plan_id[40]{};
  bool demo = false;
  std::array<StrategyLap, kLapCount> laps{};
};

enum class StrategyCue : uint8_t { On, Off, LapEnd };
struct NextStrategyCue {
  StrategyCue cue = StrategyCue::LapEnd;
  double distance_m = 0;
};

// Strict, bounded reader for assets/strategy/README.md schema 1.
// The expected course ID and route lengths come from the active firmware course.
bool parseStrategy(const char *json, size_t length, const Course &course,
                   const char *expected_course_id, Strategy &out,
                   char *error, size_t error_capacity);
NextStrategyCue nextStrategyCue(const StrategyLap &lap, double s_m, double route_length_m);

}  // namespace vega
