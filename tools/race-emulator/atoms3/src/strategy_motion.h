#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sim {

constexpr size_t kMaxStrategyBytes = 4096;
struct CourseSpec {
  std::string course_id;
  uint8_t lap_count = 0;
  double first_m = 0, regular_m = 0, final_m = 0;
  double length(size_t lap) const {
    return lap == 0 ? first_m : lap + 1 == lap_count ? final_m : regular_m;
  }
};
struct Run { double on_m = 0, off_m = 0; };
struct StrategyLap { uint32_t target_s = 0; std::vector<Run> runs; };
struct DrivingStrategy {
  std::string plan_id;
  uint32_t total_target_s = 0;
  std::vector<StrategyLap> laps;
};
// Consume the original Tab5 schema 1/2 JSON, without a per-lap run-count cap.
// Failure leaves the previously accepted strategy intact.
bool parseDrivingStrategy(const char *json, size_t size, const CourseSpec &course,
                          DrivingStrategy &out, std::string &error);

struct MotionConfig {
  double max_speed_kmh = 30;
  double initial_speed_kmh = 0;
  double acceleration_mps2 = 0.3;
  double coast_deceleration_mps2 = 0.03;
};
bool validMotionConfig(const MotionConfig &config);
struct MotionState {
  uint8_t lap_index = 0;
  double route_s_m = 0, total_m = 0, speed_mps = 0;
  double elapsed_s = 0, lap_elapsed_s = 0;
  bool engine_on = false, finished = false, stalled = false;
};
enum class MotionEventKind { EngineOn, EngineOff, Lap, Goal, Stalled };
struct MotionEvent {
  MotionEventKind kind;
  uint8_t lap;
  double s_m, speed_kmh, elapsed_s, lap_elapsed_s;
  uint32_t target_s;
};
using MotionCallback = void (*)(const MotionEvent &, void *);

// A deterministic, piecewise constant-acceleration model. Integrates exactly
// to ON/OFF, speed-cap, zero-speed and lap boundaries, even in a delayed tick.
class Motion {
 public:
  void reset(const CourseSpec &course, const MotionConfig &config,
             const DrivingStrategy *strategy = nullptr);
  void advance(double seconds, MotionCallback callback = nullptr, void *context = nullptr);
  const MotionState &state() const { return state_; }

 private:
  void event(MotionEventKind kind, MotionCallback callback, void *context) const;
  CourseSpec course_;
  MotionConfig config_;
  const DrivingStrategy *strategy_ = nullptr;
  MotionState state_;
};

}  // namespace sim
