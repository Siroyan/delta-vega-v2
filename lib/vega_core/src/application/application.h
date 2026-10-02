#pragma once
#include "domain/course.h"
#include "domain/engine.h"
#include "domain/race.h"
#include "ports/ports.h"

namespace vega {
class Application {
 public:
  Application(IClock &clock, IEngineOutput &output, ISettingsStore &settings_store,
              ISessionRecorder &recorder, ITelemetry &telemetry, const Course &course,
              Settings settings = {});
  bool start();
  bool cancel();
  bool manualLap();
  bool manualFinish();
  void power(bool on);
  bool ignite();
  bool configure(const Settings &settings, SettingsScope scope);
  bool selectCourse(const Course &course, uint8_t index, const Settings &settings);
  void wheel(WheelInput input) { wheel_ = input; }
  void gps(const GpsFix &fix);
  void tick();
  Snapshot snapshot() const;

 private:
  void event(Event e);
  void resetFinishBranch();
  void resetGps();
  bool finish(Millis now, bool manual = false);
  IClock &clock_;
  IEngineOutput &output_;
  ISettingsStore &store_;
  ISessionRecorder &recorder_;
  ITelemetry &telemetry_;
  const Course *course_;
  uint8_t course_index_ = 0;
  Settings settings_;
  RaceSession race_;
  EngineCommands engine_;
  WheelInput wheel_{};
  GpsFix gps_{};
  bool gps_seen_ = false, output_error_ = false, settings_error_ = false;
  bool gps_on_timing_course_ = false, manual_lap_requires_progress_ = false;
  MapPosition map_{}, route_map_{};
  PassageDetector timing_, goal_;
  double finish_branch_start_s_ = 0, finish_branch_previous_s_ = 0;
  Millis finish_branch_previous_ms_ = 0;
  uint8_t finish_branch_matches_ = 0;
  Millis last_sample_ = 0;
  uint32_t settings_attempt_ = 0;
  bool settings_accepted_ = false;
};
}  // namespace vega
