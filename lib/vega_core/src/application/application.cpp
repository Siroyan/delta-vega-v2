#include "application.h"

#include <algorithm>

namespace vega {
namespace {
constexpr double kFinishBranchMinProgressM = 55;
constexpr double kFinishBranchAdvantageM = 7;
constexpr double kFinishBranchMaxLateralM = 18;
constexpr double kFinishProgressFromExitM = 100;

CourseRoute routeForLap(uint8_t lap, uint8_t lap_count) {
  return lap <= 1 ? CourseRoute::First
                  : lap >= lap_count ? CourseRoute::Final : CourseRoute::Regular;
}
}  // namespace

Application::Application(IClock &c, IEngineOutput &o, ISettingsStore &s, ISessionRecorder &r,
                         ITelemetry &t, const Course &course, Settings cfg)
    : clock_(c),
      output_(o),
      store_(s),
      recorder_(r),
      telemetry_(t),
      course_(&course),
      settings_(validSettings(cfg) ? cfg : Settings{}),
      race_(course.lapCount()) {
  output_.stopPulse();
  output_.configurePower(settings_.power_active_high);
  output_.setPower(false);
}
Snapshot Application::snapshot() const {
  Snapshot s;
  s.now_ms = clock_.now();
  s.course_index = course_index_;
  s.lap_count = race_.lapCount();
  s.settings = settings_;
  s.race = race_.reading(s.now_ms, wheel_.pulses, settings_);
  s.engine = engine_.phase();
  s.ecu_prepare_permille = engine_.preparationPermille(s.now_ms, settings_.ecu_ready_ms);
  s.wheel = wheelReading(wheel_, clock_.nowMicros(), settings_);
  s.gps = gps_;
  s.map = map_;
  s.route_map = route_map_;
  s.gps_seen = gps_seen_;
  s.gps_fresh = gps_seen_ && gps_.valid && s.now_ms >= gps_.received_ms &&
                s.now_ms - gps_.received_ms <= settings_.gps_stale_ms;
  s.manual_lap_ready = !manual_lap_requires_progress_ || !s.gps_fresh ||
                       !gps_on_timing_course_ ||
                       timing_.progress() >= settings_.min_lap_progress_m;
  s.output_error = output_error_;
  s.settings_error = settings_error_;
  s.settings_attempt = settings_attempt_;
  s.settings_accepted = settings_accepted_;
  return s;
}
void Application::event(Event e) {
  if (race_.phase() == RacePhase::Measuring) recorder_.event(e, snapshot());
}
void Application::resetFinishBranch() {
  finish_branch_start_s_ = finish_branch_previous_s_ = 0;
  finish_branch_previous_ms_ = 0;
  finish_branch_matches_ = 0;
  goal_.reset();
}
void Application::resetGps() {
  gps_ = {};
  gps_seen_ = false;
  gps_on_timing_course_ = false;
  manual_lap_requires_progress_ = false;
  map_ = {};
  route_map_ = {};
  timing_.reset();
  resetFinishBranch();
}
bool Application::start() {
  auto now = clock_.now();
  if (!race_.start(now, wheel_.pulses)) return false;
  timing_.reset();
  manual_lap_requires_progress_ = false;
  resetFinishBranch();
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
  manual_lap_requires_progress_ = false;
  resetFinishBranch();
  return true;
}
bool Application::manualLap() {
  if (!snapshot().manual_lap_ready) return false;
  if (!race_.advance(clock_.now(), true, settings_)) return false;
  timing_.reset();
  manual_lap_requires_progress_ = false;
  resetFinishBranch();
  if (gps_seen_ && gps_.valid) {
    route_map_ = course_->locateOn(gps_.position, settings_.course_corridor_m,
                                  routeForLap(race_.lap(), race_.lapCount()));
    map_ = route_map_;
  }
  event(Event::ManualLap);
  return true;
}
bool Application::finish(Millis now, bool manual) {
  if (!race_.finish(now, wheel_.pulses, settings_)) return false;
  auto s = snapshot();
  recorder_.sample(s);
  telemetry_.publish(s);
  recorder_.event(manual ? Event::ManualFinish : Event::Finished, s);
  recorder_.end(EndReason::Finished, s);
  return true;
}
bool Application::manualFinish() { return finish(clock_.now(), true); }
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
bool Application::configure(const Settings &requested, SettingsScope scope) {
  ++settings_attempt_;
  settings_accepted_ = false;
  Settings s = settings_;
  if (scope == SettingsScope::General)
    applyGeneral(s, generalSettings(requested));
  else if (scope == SettingsScope::Course)
    applyCourse(s, courseSettings(requested));
  else
    return false;
  if (race_.phase() == RacePhase::Measuring || !validSettings(s)) return false;
  if (engine_.phase() != EnginePhase::Off && (s.power_active_high != settings_.power_active_high ||
                                              s.ecu_ready_ms != settings_.ecu_ready_ms ||
                                              s.ignition_pulse_ms != settings_.ignition_pulse_ms))
    return false;
  const bool saved = scope == SettingsScope::General
                         ? store_.saveGeneral(generalSettings(s))
                         : store_.saveCourse(courseSettings(s));
  if (!saved) {
    settings_error_ = true;
    return false;
  }
  if (s.power_active_high != settings_.power_active_high)
    output_.configurePower(s.power_active_high);
  const bool gps_source_changed = s.gps_source != settings_.gps_source;
  settings_ = s;
  settings_error_ = false;
  settings_accepted_ = true;
  timing_.reset();
  resetFinishBranch();
  if (gps_source_changed) resetGps();
  return true;
}
bool Application::selectCourse(const Course &course, uint8_t index, const Settings &s) {
  Settings combined = s;
  applyGeneral(combined, generalSettings(settings_));
  if (race_.phase() != RacePhase::Waiting || engine_.phase() != EnginePhase::Off ||
      !validSettings(combined) || !race_.setLapCount(course.lapCount())) return false;
  output_.stopPulse();
  output_.configurePower(combined.power_active_high);
  output_.setPower(false);
  course_ = &course;
  course_index_ = index;
  settings_ = combined;
  resetGps();
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
    timing_.suspend();
    resetFinishBranch();
    return;
  }
  const CourseRoute active_route = routeForLap(race_.lap(), race_.lapCount());
  const auto course_position = course_->locate(fix.position, settings_.course_corridor_m);
  gps_on_timing_course_ = course_position.on_course;
  route_map_ = course_->locateOn(fix.position, settings_.course_corridor_m, active_route);
  map_ = route_map_;
  if (active_route == CourseRoute::First && course_position.lateral_m < map_.lateral_m)
    map_ = course_position;
  if (race_.phase() != RacePhase::Measuring) return;
  if (race_.lap() < race_.lapCount()) {
    // The editable timing coordinate always belongs to the oval. First-lap
    // GPS can follow the approach, but the lap gate remains on the main track.
    const auto timing_gate = course_->locate(settings_.timing, settings_.course_corridor_m);
    if (timing_gate.on_course &&
        timing_.update(course_position, fix.received_ms, timing_gate.s_m,
                       settings_.min_lap_progress_m, *course_, settings_) &&
        race_.advance(fix.received_ms, false, settings_)) {
      timing_.reset();
      manual_lap_requires_progress_ = true;
      resetFinishBranch();
      route_map_ = course_->locateOn(fix.position, settings_.course_corridor_m,
                                    routeForLap(race_.lap(), race_.lapCount()));
      map_ = route_map_;
      event(Event::GpsLap);
    }
    return;
  }

  if (!course_->hasRoute(CourseRoute::FinishApproach)) {
    const auto goal_gate = course_->locate(settings_.goal, settings_.course_corridor_m);
    if (goal_gate.on_course &&
        goal_.update(map_, fix.received_ms, goal_gate.s_m, 100, *course_, settings_))
      finish(fix.received_ms);
    return;
  }

  // The finish branch stays only 27 m from the oval, inside the normal 60 m
  // map corridor. Require two forward GPS samples that favor the branch before
  // accepting its goal gate. A vehicle continuing on the oval cannot finish.
  const auto finish_position = course_->locateOn(fix.position, settings_.course_corridor_m,
                                                 CourseRoute::FinishApproach);
  const auto goal_gate = course_->locateOn(settings_.goal, settings_.course_corridor_m,
                                          CourseRoute::FinishApproach);
  const bool on_finish_branch =
      finish_position.s_m >= kFinishBranchMinProgressM &&
      finish_position.lateral_m <= std::min(kFinishBranchMaxLateralM,
                                            settings_.course_corridor_m) &&
      finish_position.lateral_m + kFinishBranchAdvantageM < course_position.lateral_m;
  if (!on_finish_branch || !goal_gate.on_course) {
    resetFinishBranch();
    return;
  }
  const bool continuing = finish_branch_matches_ &&
                          fix.received_ms >= finish_branch_previous_ms_ &&
                          fix.received_ms - finish_branch_previous_ms_ <= settings_.gps_stale_ms &&
                          finish_position.s_m >= finish_branch_previous_s_ &&
                          finish_position.s_m - finish_branch_previous_s_ <= settings_.max_gps_step_m;
  if (!continuing) {
    resetFinishBranch();
    finish_branch_start_s_ = finish_position.s_m;
    finish_branch_matches_ = 1;
  } else if (finish_position.s_m >= finish_branch_previous_s_ + 2 &&
             finish_branch_matches_ < 2) {
    ++finish_branch_matches_;
  }
  finish_branch_previous_s_ = finish_position.s_m;
  finish_branch_previous_ms_ = fix.received_ms;
  const double remaining_progress =
      std::max(0.0, kFinishProgressFromExitM - finish_branch_start_s_);
  const bool crossed_goal = goal_.update(finish_position, fix.received_ms, goal_gate.s_m,
                                         remaining_progress, *course_, settings_,
                                         CourseRoute::FinishApproach);
  if (finish_branch_matches_ >= 2 && crossed_goal) finish(fix.received_ms);
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
