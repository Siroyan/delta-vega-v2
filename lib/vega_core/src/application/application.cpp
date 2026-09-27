#include "application.h"

namespace vega {
Application::Application(IClock &c, IEngineOutput &o, ISettingsStore &s, ISessionRecorder &r,
                         ITelemetry &t, const Course &course, Settings cfg)
    : clock_(c),
      output_(o),
      store_(s),
      recorder_(r),
      telemetry_(t),
      course_(course),
      settings_(validSettings(cfg) ? cfg : Settings{}) {
  output_.stopPulse();
  output_.configurePower(settings_.power_active_high);
  output_.setPower(false);
}
Snapshot Application::snapshot() const {
  Snapshot s;
  s.now_ms = clock_.now();
  s.settings = settings_;
  s.race = race_.reading(s.now_ms, wheel_.pulses, settings_);
  s.engine = engine_.phase();
  s.wheel = wheelReading(wheel_, s.now_ms * 1000, settings_);
  s.gps = gps_;
  s.map = map_;
  s.gps_seen = gps_seen_;
  s.gps_fresh = gps_seen_ && gps_.valid && s.now_ms >= gps_.received_ms &&
                s.now_ms - gps_.received_ms <= settings_.gps_stale_ms;
  s.output_error = output_error_;
  s.settings_error = settings_error_;
  s.settings_attempt = settings_attempt_;
  s.settings_accepted = settings_accepted_;
  return s;
}
void Application::event(Event e) {
  if (race_.phase() == RacePhase::Measuring) recorder_.event(e, snapshot());
}
bool Application::start() {
  auto now = clock_.now();
  if (!race_.start(now, wheel_.pulses)) return false;
  timing_.reset();
  goal_.reset();
  last_sample_ = now;
  auto s = snapshot();
  recorder_.begin(s.race.session, settings_, now);
  recorder_.event(Event::Started, s);
  recorder_.sample(s);
  telemetry_.publish(s);
  return true;
}
bool Application::cancel() {
  if (race_.phase() != RacePhase::Measuring) return false;
  auto s = snapshot();
  recorder_.event(Event::Cancelled, s);
  recorder_.end(EndReason::Cancelled, s);
  race_.cancel();
  timing_.reset();
  goal_.reset();
  return true;
}
bool Application::manualLap() {
  if (!race_.advance(clock_.now(), true, settings_)) return false;
  timing_.reset();
  event(Event::ManualLap);
  return true;
}
void Application::power(bool on) {
  bool was_on = engine_.phase() != EnginePhase::Off;
  engine_.power(on, clock_.now(), settings_);
  if (!on) {
    output_.stopPulse();
    output_error_ = false;
  }
  output_.setPower(on);
  if (on != was_on) event(on ? Event::PowerOn : Event::PowerOff);
}
bool Application::ignite() {
  if (!engine_.ignite(clock_.now(), settings_)) return false;
  if (!output_.pulse(settings_.ignition_pulse_ms)) output_error_ = true;
  event(Event::Ignition);
  return !output_error_;
}
bool Application::configure(const Settings &s) {
  ++settings_attempt_;
  settings_accepted_ = false;
  if (race_.phase() == RacePhase::Measuring || !validSettings(s)) return false;
  if (engine_.phase() != EnginePhase::Off && (s.power_active_high != settings_.power_active_high ||
                                              s.ecu_ready_ms != settings_.ecu_ready_ms ||
                                              s.ignition_pulse_ms != settings_.ignition_pulse_ms))
    return false;
  if (!store_.save(s)) {
    settings_error_ = true;
    return false;
  }
  if (s.power_active_high != settings_.power_active_high)
    output_.configurePower(s.power_active_high);
  settings_ = s;
  settings_error_ = false;
  settings_accepted_ = true;
  timing_.reset();
  goal_.reset();
  return true;
}
void Application::gps(const GpsFix &fix) {
  auto now = clock_.now();
  if (fix.received_ms > now) return;
  if (gps_seen_ && fix.received_ms <= gps_.received_ms) return;
  gps_seen_ = true;
  gps_ = fix;
  if (!fix.valid || !validGeo(fix.position) || fix.received_ms > now ||
      now - fix.received_ms > settings_.gps_stale_ms) {
    gps_.valid = false;
    timing_.reset();
    goal_.reset();
    return;
  }
  map_ = course_.locate(fix.position, settings_.course_corridor_m);
  if (race_.phase() != RacePhase::Measuring) return;
  auto timing_s = course_.locate(settings_.timing, settings_.course_corridor_m);
  auto goal_s = course_.locate(settings_.goal, settings_.course_corridor_m);
  bool crossed_timing =
      timing_s.on_course && timing_.update(map_, fix.received_ms, timing_s.s_m,
                                           settings_.min_lap_progress_m, course_, settings_);
  bool crossed_goal =
      goal_s.on_course && goal_.update(map_, fix.received_ms, goal_s.s_m, 100, course_, settings_);
  if (crossed_timing && race_.advance(fix.received_ms, false, settings_)) event(Event::GpsLap);
  if (crossed_goal && race_.finish(fix.received_ms, wheel_.pulses, settings_)) {
    auto s = snapshot();
    recorder_.sample(s);
    telemetry_.publish(s);
    recorder_.event(Event::Finished, s);
    recorder_.end(EndReason::Finished, s);
  }
}
void Application::tick() {
  auto now = clock_.now();
  auto before = engine_.phase();
  engine_.tick(now);
  if (before == EnginePhase::Pulsing && engine_.phase() == EnginePhase::Issued) output_.stopPulse();
  if (race_.phase() == RacePhase::Measuring && now - last_sample_ >= 500) {
    last_sample_ = now;
    auto s = snapshot();
    recorder_.sample(s);
    telemetry_.publish(s);
  }
}
}  // namespace vega
