#include "presenter.h"

#include <cstdio>
#include <cstring>

namespace vega {
void Presenter::formatTime(uint64_t ms, char *out, size_t cap) {
  uint64_t seconds = ms / 1000;
  if (seconds >= 3600)
    std::snprintf(out, cap, "%llu:%02llu:%02llu", (unsigned long long)(seconds / 3600),
                  (unsigned long long)(seconds / 60 % 60), (unsigned long long)(seconds % 60));
  else
    std::snprintf(out, cap, "%02llu:%02llu", (unsigned long long)(seconds / 60),
                  (unsigned long long)(seconds % 60));
}
void Presenter::render(const Snapshot &s, const UiStatus &status, const Strategy *strategy) {
  settings_ = s.settings;
  phase_ = s.race.phase;
  DisplayModel m;
  m.phase = s.race.phase;
  m.lap_count = lap_count_;
  if (s.wheel.valid)
    std::snprintf(m.speed, sizeof(m.speed), "%.1f", s.wheel.speed_kmh);
  else
    std::snprintf(m.speed, sizeof(m.speed), "--.-");
  if (s.race.phase != RacePhase::Waiting && s.wheel.pulses > 0)
    std::snprintf(m.average, sizeof(m.average), "%.1f", s.race.average_kmh);
  else
    std::snprintf(m.average, sizeof(m.average), "--.-");
  if (s.race.lap)
    std::snprintf(m.lap, sizeof(m.lap), "%u / %u", s.race.lap, lap_count_);
  else
    std::snprintf(m.lap, sizeof(m.lap), "- / %u", lap_count_);
  formatTime(s.race.total_ms, m.total, sizeof(m.total));
  formatTime(s.race.lap_ms, m.lap_time, sizeof(m.lap_time));
  char time[24];
  formatTime(uint64_t(s.settings.total_target_s) * 1000, time, sizeof(time));
  std::snprintf(m.total_target, sizeof(m.total_target), "TARGET %s", time);
  auto lap = s.race.lap ? s.race.lap : 1;
  const size_t target_index = static_cast<size_t>(lap) > kLapCount ? kLapCount - 1 : lap - 1;
  formatTime(uint64_t(s.settings.lap_target_s[target_index]) * 1000, time, sizeof(time));
  std::snprintf(m.lap_target, sizeof(m.lap_target), "TARGET %s", time);
  m.power_on = s.engine != EnginePhase::Off;
  m.ignition_preparing = s.engine == EnginePhase::Preparing;
  m.ignition_prepare_permille = s.ecu_prepare_permille;
  m.display_brightness = static_cast<uint8_t>(s.settings.display_brightness);
  m.ignition_enabled = s.engine == EnginePhase::Ready && !s.output_error;
  m.finish_mode = m.phase == RacePhase::Measuring && lap == lap_count_;
  m.lap_enabled = m.phase == RacePhase::Measuring &&
                  (m.finish_mode || (s.race.lap_ms >= s.settings.lap_duplicate_ms &&
                                     s.manual_lap_ready));
  m.heartbeat = (s.now_ms / 500) % 2;
  m.pulse = s.wheel.pulse_recent;
  m.gps_ok = s.gps_fresh;
  m.position_visible = s.gps_seen && s.map.x >= 0 && s.map.x <= 480 && s.map.y >= 0 &&
                       s.map.y <= 480 && (s.map.x != 0 || s.map.y != 0);
  m.position_stale = !s.gps_fresh;
  m.marker_x = int(s.map.x);
  m.marker_y = int(s.map.y);
  m.marker_heading = s.gps.course_deg;
  m.overtime = s.race.total_ms > uint64_t(s.settings.total_target_s) * 1000;
  std::snprintf(m.gps_status, sizeof(m.gps_status), "%s",
                s.gps_fresh  ? "GPS FIX"
                : s.gps_seen ? "GPS STALE"
                             : "GPS NO DATA");
  if (s.gps_seen && s.gps.valid && validGeo(s.gps.position)) {
    std::snprintf(m.gps_latitude, sizeof(m.gps_latitude), "LAT %.8f", s.gps.position.latitude);
    std::snprintf(m.gps_longitude, sizeof(m.gps_longitude), "LON %.8f", s.gps.position.longitude);
  } else {
    std::snprintf(m.gps_latitude, sizeof(m.gps_latitude), "LAT --");
    std::snprintf(m.gps_longitude, sizeof(m.gps_longitude), "LON --");
  }
  std::snprintf(m.map_status, sizeof(m.map_status), "%s",
                s.gps_fresh ? (s.map.on_course ? "" : "POSITION OFF COURSE")
                            : (s.gps_seen ? "POSITION NOT UPDATED" : "GPS NOT AVAILABLE"));
  std::snprintf(m.link, sizeof(m.link), "%s",
                status.mqtt_connected       ? "LINK ONLINE"
                : status.network_configured ? "LINK OFFLINE"
                                            : "LINK NOT SET");
  std::snprintf(m.race_status, sizeof(m.race_status), "%s",
                m.phase == RacePhase::Waiting    ? "WAITING"
                : m.phase == RacePhase::Finished ? "FINISHED"
                                                 : "RUNNING");
  std::snprintf(m.clock, sizeof(m.clock), "%s", status.clock);
  if (status.battery_percent >= 0 && status.battery_percent <= 100)
    std::snprintf(m.battery, sizeof(m.battery), "BATTERY %d%%", status.battery_percent);
  else
    std::snprintf(m.battery, sizeof(m.battery), "BATTERY --%%");
  std::snprintf(m.action, sizeof(m.action), "%s",
                m.phase == RacePhase::Waiting    ? "START TIMING"
                : m.phase == RacePhase::Finished ? "TIMING COMPLETE"
                                                 : "PLAN NOT SET");
  std::snprintf(m.detail, sizeof(m.detail), "%s",
                m.phase == RacePhase::Waiting    ? "MANUAL START"
                : m.phase == RacePhase::Finished ? "RESULT HELD"
                                                 : "NO OPERATION GUIDANCE");
  std::snprintf(m.plan_status, sizeof(m.plan_status), "%s",
                status.plan_state == PlanState::Invalid ? "PLAN INVALID"
                : status.plan_state == PlanState::Loading ? "PLAN LOADING"
                : status.plan_state == PlanState::Ready && strategy ? (strategy->demo ? "PLAN DEMO" : "PLAN ACTIVE")
                                                                    : "PLAN NOT SET");
  if (m.phase == RacePhase::Measuring && status.plan_state == PlanState::Invalid) {
    std::snprintf(m.action, sizeof(m.action), "PLAN INVALID");
    std::snprintf(m.detail, sizeof(m.detail), "CHECK SD STRATEGY");
  }
  if (status.plan_state == PlanState::Ready && strategy &&
      m.phase != RacePhase::Finished && s.race.lap <= kLapCount) {
    m.plan_loaded = true;
    m.plan_lap_number = s.race.lap ? s.race.lap : 1;
    m.plan_lap = strategy->laps[m.plan_lap_number - 1];
    // Waiting previews the first route while START TIMING stays prominent.
    if (m.phase == RacePhase::Measuring && s.gps_fresh && s.route_map.on_course) {
      auto next = nextStrategyCue(m.plan_lap, s.route_map.s_m,
                                  m.plan_lap.route_length_m);
      const auto meters = static_cast<unsigned>(std::ceil(next.distance_m));
      if (next.cue == StrategyCue::On)
        std::snprintf(m.action, sizeof(m.action), "NEXT ON IN %u m", meters);
      else if (next.cue == StrategyCue::Off)
        std::snprintf(m.action, sizeof(m.action), "NEXT OFF IN %u m", meters);
      else
        std::snprintf(m.action, sizeof(m.action), "COAST TO LAP END");
      std::snprintf(m.detail, sizeof(m.detail),
                    strategy->demo ? "DEMO ONLY / LAP %u" : "LAP %u PLAN / DRIVER ACTION",
                    s.race.lap);
    } else if (m.phase == RacePhase::Measuring) {
      std::snprintf(m.action, sizeof(m.action), "PLAN READY");
      std::snprintf(m.detail, sizeof(m.detail), "%s",
                    strategy->demo ? (s.gps_fresh ? "DEMO ONLY / OFF COURSE"
                                                : "DEMO ONLY / GPS UNAVAILABLE")
                                   : (s.gps_fresh ? "POSITION OFF COURSE" : "GPS UNAVAILABLE"));
    }
  }
  if (s.output_error)
    std::snprintf(m.notice, sizeof(m.notice), "OUTPUT ERROR\nCHECK ECU SIGNAL");
  else if (s.settings_error)
    std::snprintf(m.notice, sizeof(m.notice), "SETTINGS SAVE FAILED");
  else if (!s.gps_fresh && m.phase == RacePhase::Measuring &&
           (status.sd_error || !status.sd_ready))
    std::snprintf(m.notice, sizeof(m.notice), "GPS LOST: MANUAL LAP\nSD RECORDING ERROR");
  else if (!s.gps_fresh && m.phase == RacePhase::Measuring)
    std::snprintf(m.notice, sizeof(m.notice), "GPS UNAVAILABLE\nUSE MANUAL LAP");
  else if (status.sd_error || !status.sd_ready)
    std::snprintf(m.notice, sizeof(m.notice), "SD RECORDING UNAVAILABLE\nTIMING CONTINUES");
  else if (s.race.lap_approximate)
    std::snprintf(m.notice, sizeof(m.notice), "LAP CORRECTED\nMANUAL / APPROX.");
  else if (s.engine == EnginePhase::Preparing)
    std::snprintf(m.notice, sizeof(m.notice), "ECU PREPARING");
  else if (s.engine == EnginePhase::Pulsing)
    std::snprintf(m.notice, sizeof(m.notice), "START COMMAND ACTIVE");
  view_.show(m);
}
bool Presenter::request(const Command &command) {
  if (!commands_) return false;
  if (command.kind == CommandKind::Configure &&
      (phase_ == RacePhase::Measuring || !validSettings(command.settings)))
    return false;
  return commands_->submit(command);
}
}  // namespace vega
