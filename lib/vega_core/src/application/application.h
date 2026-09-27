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
  void power(bool on);
  bool ignite();
  bool configure(const Settings &settings);
  void wheel(WheelInput input) { wheel_ = input; }
  void gps(const GpsFix &fix);
  void tick();
  Snapshot snapshot() const;

 private:
  void event(Event e);
  IClock &clock_;
  IEngineOutput &output_;
  ISettingsStore &store_;
  ISessionRecorder &recorder_;
  ITelemetry &telemetry_;
  const Course &course_;
  Settings settings_;
  RaceSession race_;
  EngineCommands engine_;
  WheelInput wheel_{};
  GpsFix gps_{};
  bool gps_seen_ = false, output_error_ = false, settings_error_ = false;
  MapPosition map_{};
  PassageDetector timing_, goal_;
  Millis last_sample_ = 0;
  uint32_t settings_attempt_ = 0;
  bool settings_accepted_ = false;
};
}  // namespace vega
