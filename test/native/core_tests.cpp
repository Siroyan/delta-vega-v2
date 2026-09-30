#include <cassert>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

#include "../../src/adapters/course_data.h"
#include "../../src/control_gesture.h"
#include "application/application.h"
#include "application/telemetry_json.h"
#include "domain/nmea.h"
#include "domain/settings_codec.h"
#include "domain/strategy.h"
#include "presentation/presenter.h"
#include "presentation/settings_form.h"

using namespace vega;
struct Clock : IClock {
  Millis time = 100000;
  uint64_t micros_offset = 0;
  Millis now() const override { return time; }
  uint64_t nowMicros() const override { return time * 1000 + micros_offset; }
};
struct Output : IEngineOutput {
  bool on = false, high = false, active_high = true, fail = false;
  unsigned triggers = 0;
  uint32_t duration = 0;
  void configurePower(bool active) override { active_high = active; }
  void setPower(bool state) override { on = state; }
  bool pulse(uint32_t ms) override {
    ++triggers;
    if (fail) return false;
    high = true;
    duration = ms;
    return true;
  }
  void stopPulse() override { high = false; }
};
struct Store : ISettingsStore {
  bool fail = false;
  Settings saved;
  bool save(const Settings &s) override {
    saved = s;
    return !fail;
  }
};
struct Recorder : ISessionRecorder {
  unsigned begins = 0, samples = 0, ends = 0, finished_events = 0, manual_finish_events = 0;
  EndReason reason{};
  Snapshot last{};
  void begin(uint32_t, const Settings &, Millis) override { ++begins; }
  void sample(const Snapshot &s) override {
    ++samples;
    last = s;
  }
  void event(Event e, const Snapshot &) override {
    if (e == Event::Finished) ++finished_events;
    if (e == Event::ManualFinish) ++manual_finish_events;
  }
  void end(EndReason r, const Snapshot &s) override {
    ++ends;
    reason = r;
    last = s;
  }
};
struct Telemetry : ITelemetry {
  unsigned count = 0;
  void publish(const Snapshot &) override { ++count; }
};
struct View : IView {
  DisplayModel model;
  void show(const DisplayModel &m) override { model = m; }
};
const CoursePoint square[] = {{0, 0, 0}, {1000, 0, 1000}, {1000, 1000, 2000}, {0, 1000, 3000}};
const CourseData course_data{square, 4, 4000, {36, 140}, 100000, 100000, {0.4, 0, 0, 0, -0.4, 480}};
struct Fixture {
  Clock clock;
  Output output;
  Store store;
  Recorder recorder;
  Telemetry telemetry;
  Course course{course_data};
  Settings settings;
  Application app;
  Fixture()
      : settings(makeSettings()),
        app(clock, output, store, recorder, telemetry, course, settings) {}
  static Settings makeSettings() {
    Settings s;
    s.timing = {36, 140.005};
    s.goal = {36.005, 140};
    return s;
  }
  void fix(double distance) {
    GpsFix g;
    g.valid = true;
    g.position = course.pointAt(distance);
    g.received_ms = clock.time;
    app.gps(g);
  }
  void travel(double from, double to) {
    for (double s = from; s <= to; s += 20) {
      clock.time += 1000;
      fix(s);
      app.tick();
    }
  }
};
void engineTest() {
  Fixture f;
  assert(!f.output.on && !f.output.high);
  assert(!f.app.ignite());
  f.app.power(true);
  assert(f.output.on);
  assert(!f.app.ignite());
  f.clock.time += 999;
  f.app.tick();
  assert(!f.app.ignite());
  ++f.clock.time;
  f.app.tick();
  assert(f.app.ignite());
  assert(f.output.triggers == 1 && f.output.duration == 1000);
  assert(!f.app.ignite());
  f.app.power(true);
  assert(!f.app.ignite());
  f.clock.time += 999;
  f.app.tick();
  assert(f.output.high);
  ++f.clock.time;
  f.app.tick();
  assert(!f.output.high && f.app.snapshot().engine == EnginePhase::Issued);
  assert(!f.app.ignite());
  f.app.power(false);
  assert(!f.output.on);
  f.app.power(true);
  f.clock.time += 1000;
  f.app.tick();
  assert(f.app.ignite());
  f.app.power(false);
  assert(!f.output.high);
  f.clock.time += 10000;
  f.app.tick();
  assert(f.app.snapshot().engine == EnginePhase::Off);
  f.output.fail = true;
  f.app.power(true);
  f.clock.time += 1000;
  f.app.tick();
  assert(!f.app.ignite());
  assert(f.app.snapshot().output_error && !f.app.ignite());
  f.app.power(false);
  f.output.fail = false;
  assert(!f.app.snapshot().output_error);
  f.app.power(true);
  f.clock.time += 1000;
  f.app.tick();
  assert(f.app.ignite());
}
void raceTest() {
  Fixture f;
  f.app.wheel({100, 0, 0});
  assert(f.app.start());
  assert(!f.app.start());
  assert(f.app.snapshot().race.lap == 1 && f.app.snapshot().race.total_ms == 0);
  f.clock.time += 1000;
  f.app.wheel({110, 100999000, 100899000});
  auto s = f.app.snapshot();
  assert(std::abs(s.race.distance_m - 10.3) < 1e-9);
  assert(std::abs(s.race.average_kmh - 37.08) < 1e-9);
  f.clock.time += 1000;
  s = f.app.snapshot();
  assert(std::abs(s.race.average_kmh - 18.54) < 1e-9);
  f.app.power(true);
  f.app.power(false);
  assert(f.app.snapshot().race.phase == RacePhase::Measuring);
  Settings cfg = f.settings;
  cfg.total_target_s = 2400;
  assert(!f.app.configure(cfg));
  for (int i = 0; i < 6; ++i) {
    f.clock.time += 10000;
    assert(f.app.manualLap());
    assert(!f.app.manualLap());
  }
  assert(f.app.snapshot().race.lap == 7 && f.app.snapshot().race.phase == RacePhase::Measuring);
  assert(!f.app.manualLap());
  assert(f.app.cancel());
  assert(f.recorder.reason == EndReason::Cancelled);
  assert(f.app.start());
  assert(f.app.snapshot().race.session == 2);
  assert(f.app.snapshot().race.total_ms == 0);
}
void manualFinishTest() {
  Fixture f;
  assert(!f.app.manualFinish());
  assert(f.app.start());
  assert(!f.app.manualFinish());
  for (int lap = 1; lap < 7; ++lap) {
    f.clock.time += 10000;
    if (lap == 6) assert(!f.app.manualFinish());
    assert(f.app.manualLap());
  }
  assert(f.app.snapshot().race.lap == 7);
  // The confirmation dialog guards manual finish; no forced delay changes its timestamp.
  const auto expected_total = f.app.snapshot().race.total_ms;
  assert(f.app.manualFinish());
  assert(f.app.snapshot().race.phase == RacePhase::Finished);
  assert(f.app.snapshot().race.total_ms == expected_total);
  assert(f.recorder.ends == 1 && f.recorder.manual_finish_events == 1 &&
         f.recorder.finished_events == 0);
  assert(f.recorder.reason == EndReason::Finished);
  assert(f.recorder.last.race.total_ms == expected_total);
  assert(!f.app.manualFinish());
  f.clock.time += 300000;
  assert(f.app.snapshot().race.total_ms == expected_total);
  assert(!f.app.cancel() && !f.app.start());
}
void gpsRaceTest() {
  Fixture f;
  assert(f.app.start());
  f.fix(700);
  // Goal before lap 7 is ignored; each full circuit crosses timing once.
  for (int lap = 0; lap < 6; ++lap) f.travel(720 + lap * 4000, 4700 + lap * 4000);
  assert(f.app.snapshot().race.lap == 7);
  assert(f.app.snapshot().race.phase == RacePhase::Measuring);
  f.travel(24720, 27600);
  assert(f.app.snapshot().race.phase == RacePhase::Finished);
  assert(f.recorder.reason == EndReason::Finished);
  auto result = f.app.snapshot().race;
  f.clock.time += 300000;
  f.app.tick();
  assert(f.app.snapshot().race.total_ms == result.total_ms);
  assert(!f.app.start());
  assert(!f.app.cancel());
}
void passageTest() {
  Fixture f;
  PassageDetector d;
  auto at = [&](double s) { return f.course.locate(f.course.pointAt(s), 60); };
  assert(!d.update(at(600), 0, 500, 0, f.course, f.settings));
  assert(!d.update(at(490), 1000, 500, 0, f.course, f.settings));    // jump rejected
  assert(!d.update(at(510), 2000, 500, 100, f.course, f.settings));  // insufficient progress
  d.reset();
  assert(!d.update(at(520), 0, 500, 0, f.course, f.settings));
  assert(!d.update(at(480), 1000, 500, 0, f.course, f.settings));  // wrong direction
  assert(!d.update(at(490), 5000, 500, 0, f.course, f.settings));  // stale interval rearms
  MapPosition off = at(510);
  off.on_course = false;
  assert(!d.update(off, 6000, 500, 0, f.course, f.settings));
  d.reset();
  assert(!d.update(at(3980), 0, 5, 0, f.course, f.settings));
  assert(d.update(at(20), 1000, 5, 0, f.course, f.settings));  // closing segment
}
void gpsOutageAndManualLapTest() {
  for (int scenario = 0; scenario < 4; ++scenario) {
    Fixture f;
    assert(f.app.start());
    f.fix(700);
    f.travel(720, 4300);
    if (scenario == 0) {
      f.clock.time += 5000;  // A short gap does not erase progress.
    } else if (scenario == 1) {
      GpsFix invalid;
      invalid.received_ms = ++f.clock.time;
      f.app.gps(invalid);
    } else if (scenario == 2) {
      GpsFix off_course;
      off_course.valid = true;
      off_course.received_ms = ++f.clock.time;
      off_course.position = {36.1, 140.1};
      f.app.gps(off_course);
    } else {
      f.clock.time += 1000;
      f.fix(2000);  // A single impossible GPS jump is discarded.
    }
    f.clock.time += 1000;
    f.fix(4320);  // Rearm without inferring distance across the missing segment.
    f.travel(4340, 4520);
    assert(f.app.snapshot().race.lap == 2);
  }
  {
    Fixture f;
    assert(f.app.start());
    f.fix(700);
    f.travel(720, 4300);
    f.clock.time += 31000;
    f.fix(4320);
    f.travel(4340, 4520);
    assert(f.app.snapshot().race.lap == 1);  // A long outage discards old progress.
  }
  {
    Fixture f;
    assert(f.app.start());
    f.fix(700);
    f.travel(720, 4380);
    GpsFix invalid;
    invalid.received_ms = ++f.clock.time;
    f.app.gps(invalid);
    f.clock.time += 1000;
    f.fix(4520);  // The gate passed while GPS was invalid; do not invent a crossing.
    f.travel(4540, 4700);
    assert(f.app.snapshot().race.lap == 1);
  }
  {
    Fixture f;
    assert(f.app.start());
    f.fix(700);
    f.travel(720, 4700);
    assert(f.app.snapshot().race.lap == 2);  // GPS has already counted this lap.
    f.clock.time += f.settings.lap_duplicate_ms;
    f.fix(4720);
    assert(!f.app.snapshot().manual_lap_ready);
    assert(!f.app.manualLap());
    View view;
    Presenter presenter(view);
    presenter.render(f.app.snapshot(), {});
    assert(!view.model.lap_enabled);
    f.clock.time += f.settings.gps_stale_ms + 1;
    assert(f.app.snapshot().manual_lap_ready);  // Manual correction remains available without GPS.
  }
}
void wheelTest() {
  Settings s;
  assert(!wheelReading({}, 1000000, s).valid);
  auto r = wheelReading({2, 2000000, 1000000}, 2000000, s);
  assert(r.valid && std::abs(r.speed_kmh - 3.708) < 1e-9);
  r = wheelReading({2, 2000000, 1000000}, 6000000, s);
  assert(r.valid && r.speed_kmh == 0 && !r.pulse_recent);
  RaceSession race;
  assert(race.start(0, 100));
  assert(race.reading(1000, 105, s).distance_m == 5.15);
  Fixture f;
  f.clock.micros_offset = 999;
  f.app.wheel({2, 100000900, 99000900});
  assert(f.app.snapshot().wheel.valid);  // A pulse later in the same millisecond is valid.
}
std::string sentence(std::string body) {
  unsigned sum = 0;
  for (auto c : body) sum ^= c;
  char suffix[8];
  std::snprintf(suffix, sizeof(suffix), "*%02X\r\n", sum);
  return "$" + body + suffix;
}
void nmeaTest() {
  NmeaParser parser;
  GpsFix fix;
  bool emitted = false;
  auto input = sentence("GNRMC,123456.00,A,3631.96596,N,14013.57614,E,10.0,120.0,280926,,,A");
  for (auto c : input) emitted = parser.feed(c, 1000, fix) || emitted;
  assert(emitted && fix.valid);
  assert(std::abs(fix.position.latitude - 36.532766) < 1e-8);
  assert(std::abs(fix.position.longitude - 140.226269) < 1e-8);
  assert(std::abs(fix.speed_kmh - 18.52) < 1e-8);
  assert(fix.has_heading);
  input[5] = 'X';
  emitted = false;
  for (auto c : input) emitted = parser.feed(c, 2000, fix) || emitted;
  assert(!emitted && parser.rejected() == 1);
  emitted = false;
  for (auto c : sentence("GPRMC,123457.00,V,,,,,,,280926,,,N"))
    emitted = parser.feed(c, 3000, fix) || emitted;
  assert(emitted && !fix.valid);
  for (auto c : std::string("$") + std::string(300, 'A') + "\n") parser.feed(c, 4000, fix);
  assert(parser.rejected() >= 2);
}
void settingsTest() {
  Fixture f;
  Settings cfg = f.settings;
  cfg.total_target_s = 2356;
  cfg.goal = {36.01, 140};
  assert(f.app.configure(cfg));
  assert(f.app.snapshot().settings.total_target_s == 2356);
  cfg.lap_target_s[2] = 0;
  assert(!f.app.configure(cfg));
  cfg = f.settings;
  cfg.goal.latitude = std::numeric_limits<double>::quiet_NaN();
  assert(!f.app.configure(cfg));
  f.store.fail = true;
  assert(!f.app.configure(f.settings));
  assert(f.app.snapshot().settings.total_target_s == 2356);
  assert(f.app.snapshot().settings_attempt == 4 && !f.app.snapshot().settings_accepted);
  f.store.fail = false;
  f.app.power(true);
  assert(f.app.configure(f.settings));
  assert(f.output.on);
  cfg = f.settings;
  cfg.power_active_high = false;
  assert(!f.app.configure(cfg));
  f.app.power(false);
  assert(f.app.configure(cfg));
  assert(!f.output.on && !f.output.active_high);
}
void presenterTest() {
  Fixture f;
  View view;
  Presenter presenter(view);
  UiStatus status;
  presenter.render(f.app.snapshot(), status);
  assert(std::strcmp(view.model.speed, "--.-") == 0);
  assert(std::strcmp(view.model.total_target, "TARGET 42:00") == 0);
  assert(std::strcmp(view.model.lap_target, "TARGET 06:00") == 0);
  assert(!view.model.position_visible);
  assert(!view.model.finish_mode && !view.model.lap_enabled);
  char time[24];
  Presenter::formatTime(3600123, time, sizeof(time));
  assert(std::strcmp(time, "1:00:00") == 0);
  f.app.power(true);
  f.clock.time += 1000;
  f.app.tick();
  presenter.render(f.app.snapshot(), status);
  assert(view.model.ignition_enabled);
  f.app.ignite();
  presenter.render(f.app.snapshot(), status);
  assert(!view.model.ignition_enabled);
  f.fix(1200);
  f.clock.time += 4000;
  presenter.render(f.app.snapshot(), status);
  assert(!view.model.gps_ok && view.model.position_stale);
  assert(f.app.start());
  presenter.render(f.app.snapshot(), status);
  assert(std::strcmp(view.model.notice, "GPS LOST: MANUAL LAP\nSD RECORDING ERROR") == 0);
  char json[1024];
  assert(telemetryJson(f.app.snapshot(), json, sizeof(json), "pi", "quoted \"note\"") > 0);
  assert(std::strstr(json, "\"speed\":null") && std::strstr(json, "\"latitude\":null"));
  assert(std::strstr(json, "\"average_speed\":null"));
  assert(std::strstr(json, "\\\"note\\\""));
  assert(!telemetryJson(f.app.snapshot(), json, 8));
  for (int lap = 1; lap < 7; ++lap) {
    f.clock.time += 10000;
    assert(f.app.manualLap());
    presenter.render(f.app.snapshot(), status);
    assert(view.model.finish_mode == (lap == 6));
  }
  presenter.render(f.app.snapshot(), status);
  assert(view.model.finish_mode && view.model.lap_enabled);
}
void settingsFormTest() {
  Settings s;
  char value[32];
  assert(editSetting(s, 0, "39:16") && s.total_target_s == 2356);
  assert(editSetting(s, 1, "1:02:03") && s.lap_target_s[0] == 3723);
  assert(!editSetting(s, 1, "6:60") && !editSetting(s, 1, "0:00"));
  assert(!editSetting(s, 1, "99999:00") && !editSetting(s, 1, "06:00junk"));
  assert(!editSetting(s, 1, "6::00") && !editSetting(s, 1, "6"));
  for (size_t i = 8; i < 14; ++i) {
    assert(editSetting(s, i, i % 2 == 0 ? "36.532766" : "140.226269"));
    settingText(s, i, value, sizeof(value));
    assert(std::strcmp(value, i % 2 == 0 ? "36.53276600" : "140.22626900") == 0);
  }
  assert(!editSetting(s, 8, "91") && !editSetting(s, 9, "181"));
  assert(!editSetting(s, 8, "NaN") && !editSetting(s, 8, "36e0"));
  assert(!editSetting(s, 8, "36..5") && !editSetting(s, 8, " 36"));
  assert(!editSetting(s, 99, "6:00"));
  assert(editSetting(s, 8, "-36.0") && s.start.latitude == -36);
  struct AdvancedCase { size_t field; const char *input; const char *formatted; };
  const AdvancedCase advanced[] = {
      {14, "1.034", "1.03400000"}, {15, "2", "2"},
      {16, "2500", "2500"}, {17, "750", "750"},
      {18, "0", "0"}, {19, "3500", "3500"},
      {20, "4000", "4000"}, {21, "5000", "5000"},
      {22, "55.5", "55.50000000"}, {23, "90.25", "90.25000000"},
      {24, "650.75", "650.75000000"}, {25, "70000", "70000"},
      {26, "12000", "12000"}};
  for (auto item : advanced) {
    assert(editSetting(s, item.field, item.input));
    settingText(s, item.field, value, sizeof(value));
    assert(std::strcmp(value, item.formatted) == 0);
    assert(editSetting(s, item.field, value));
  }
  assert(!editSetting(s, 14, "0.09") && !editSetting(s, 14, "1e2"));
  assert(!editSetting(s, 15, "0") && !editSetting(s, 15, "2.5"));
  assert(!editSetting(s, 16, "60001") && !editSetting(s, 17, "0"));
  assert(!editSetting(s, 18, "2") && !editSetting(s, 18, "01"));
  assert(!editSetting(s, 19, "99") && !editSetting(s, 20, "499"));
  assert(!editSetting(s, 21, "30001") && !editSetting(s, 22, "0.9"));
  assert(!editSetting(s, 23, "201") && !editSetting(s, 24, "5001"));
  assert(!editSetting(s, 25, "3600001") && !editSetting(s, 26, "499"));
  assert(!editSetting(s, 25, "4294967296") && !editSetting(s, 22, "1..2"));
}
void gpsSourceSettingsTest() {
  Settings old;
  old.version = 1;
  old.total_target_s = 2356;
  old.lap_target_s[0] = 250;
  old.start = {35.123456, 139.654321};
  old.timing = {35.234567, 139.765432};
  old.goal = {35.345678, 139.876543};
  old.wheel_circumference_m = 1.234;
  old.pulses_per_revolution = 2;
  old.ecu_ready_ms = 1234;
  old.ignition_pulse_ms = 987;
  old.power_active_high = false;
  old.pulse_debounce_us = 4321;
  old.speed_zero_ms = 4567;
  old.gps_stale_ms = 5678;
  old.course_corridor_m = 45.5;
  old.max_gps_step_m = 67.5;
  old.min_lap_progress_m = 321.5;
  old.min_lap_ms = 65000;
  old.lap_duplicate_ms = 12000;
  std::array<uint8_t, 160> legacy{};
  std::memcpy(legacy.data(), &old, legacy.size());
  Settings migrated;
  assert(decodeSettingsBlob(legacy.data(), legacy.size(), migrated));
  assert(migrated.version == 2 && migrated.gps_source == GpsSource::M5Bus);
  old.version = 2;
  for (size_t field = 0; field < kSettingsFieldCount; ++field) {
    char before[32], after[32];
    settingText(old, field, before, sizeof(before));
    settingText(migrated, field, after, sizeof(after));
    assert(std::strcmp(before, after) == 0);
  }
  migrated.gps_source = GpsSource::PortA;
  Settings restored;
  assert(decodeSettingsBlob(&migrated, sizeof(migrated), restored));
  assert(restored.gps_source == GpsSource::PortA && restored.total_target_s == 2356);
  migrated.gps_source = static_cast<GpsSource>(9);
  assert(!decodeSettingsBlob(&migrated, sizeof(migrated), restored));
  assert(restored.gps_source == GpsSource::PortA);

  Fixture f;
  f.fix(500);
  assert(f.app.snapshot().gps_seen);
  Settings changed = f.app.snapshot().settings;
  changed.gps_source = GpsSource::PortA;
  assert(f.app.configure(changed));
  assert(!f.app.snapshot().gps_seen && !f.app.snapshot().gps_fresh);
  assert(f.app.snapshot().settings.gps_source == GpsSource::PortA);
  assert(f.app.start());
  changed.gps_source = GpsSource::M5Bus;
  assert(!f.app.configure(changed));
  assert(f.app.snapshot().settings.gps_source == GpsSource::PortA);
}
void strategyTest() {
  std::ifstream input("assets/strategy/motegi_demo.json");
  assert(input);
  const std::string json((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
  Course course(tab5::course_data);
  Strategy plan;
  char error[80]{};
  assert(parseStrategy(json.data(), json.size(), course, tab5::course_data.id, plan,
                       error, sizeof(error)));
  assert(std::strcmp(plan.plan_id, "dummy-race-002") == 0);
  assert(plan.demo);
  assert(plan.laps[0].route == CourseRoute::First && plan.laps[0].runs[0].on_s_m == 0);
  assert(plan.laps[6].route == CourseRoute::Final);
  for (const auto &lap : plan.laps) assert(lap.run_count == 3);
  auto next = nextStrategyCue(plan.laps[0], 0, plan.laps[0].route_length_m);
  assert(next.cue == StrategyCue::On && next.distance_m == 0);
  next = nextStrategyCue(plan.laps[0], 100, plan.laps[0].route_length_m);
  assert(next.cue == StrategyCue::Off && next.distance_m == 200);
  next = nextStrategyCue(plan.laps[0], 500, plan.laps[0].route_length_m);
  assert(next.cue == StrategyCue::On && next.distance_m == 220);
  next = nextStrategyCue(plan.laps[0], 800, plan.laps[0].route_length_m);
  assert(next.cue == StrategyCue::Off && next.distance_m == 150);
  next = nextStrategyCue(plan.laps[0], 1100, plan.laps[0].route_length_m);
  assert(next.cue == StrategyCue::On && next.distance_m == 330);
  next = nextStrategyCue(plan.laps[0], 1800, plan.laps[0].route_length_m);
  assert(next.cue == StrategyCue::LapEnd && next.distance_m > 300);
  View view;
  Presenter presenter(view);
  UiStatus status;
  status.plan_state = PlanState::Ready;
  Snapshot snapshot{};
  presenter.render(snapshot, status, &plan);
  assert(view.model.plan_loaded && view.model.plan_lap_number == 1);
  assert(std::strcmp(view.model.action, "START TIMING") == 0);
  snapshot.race.phase = RacePhase::Measuring;
  snapshot.race.lap = 1;
  snapshot.gps_fresh = true;
  snapshot.route_map.on_course = true;
  snapshot.route_map.s_m = 100;
  presenter.render(snapshot, status, &plan);
  assert(view.model.plan_loaded && view.model.plan_lap_number == 1);
  assert(std::strcmp(view.model.plan_status, "PLAN DEMO") == 0);
  assert(std::strcmp(view.model.action, "NEXT OFF IN 200 m") == 0);
  snapshot.race.lap = 7;
  snapshot.route_map.s_m = 1900;
  presenter.render(snapshot, status, &plan);
  assert(view.model.plan_lap.route == CourseRoute::Final);
  assert(std::strcmp(view.model.action, "COAST TO LAP END") == 0);
  status.plan_state = PlanState::Invalid;
  presenter.render(snapshot, status);
  assert(!view.model.plan_loaded && std::strcmp(view.model.plan_status, "PLAN INVALID") == 0);
  auto invalid = [&](std::string altered) {
    Strategy result = plan;
    assert(!parseStrategy(altered.data(), altered.size(), course, tab5::course_data.id,
                          result, error, sizeof(error)));
    assert(error[0] && std::strcmp(result.plan_id, plan.plan_id) == 0);
  };
  std::string original_demo = json;
  auto legacy_id = original_demo.find("dummy-race-002");
  assert(legacy_id != std::string::npos);
  original_demo.replace(legacy_id, std::strlen("dummy-race-002"), "dummy-race-001");
  auto demo_type = original_demo.find("  \"plan_type\": \"demo\",\n");
  assert(demo_type != std::string::npos);
  original_demo.erase(demo_type, std::strlen("  \"plan_type\": \"demo\",\n"));
  Strategy older_demo;
  assert(parseStrategy(original_demo.data(), original_demo.size(), course,
                       tab5::course_data.id, older_demo, error, sizeof(error)));
  assert(older_demo.demo);
  auto unnamed_type = original_demo;
  auto name_at = unnamed_type.find("dummy-race-001");
  assert(name_at != std::string::npos);
  unnamed_type.replace(name_at, std::strlen("dummy-race-001"), "real-race-001");
  invalid(unnamed_type);
  auto replace = [&](const char *before, const char *after) {
    std::string changed = json;
    auto pos = changed.find(before);
    assert(pos != std::string::npos);
    changed.replace(pos, std::strlen(before), after);
    invalid(changed);
  };
  replace("motegi_oval_2025_full_v2", "wrong_course");
  replace("\"lap\": 7", "\"lap\": 6");
  replace("\"route_id\": \"final_lap\"", "\"route_id\": \"regular_lap\"");
  replace("\"off_s_m\": 300", "\"off_s_m\": 99999");
  replace("\"on_s_m\": 0", "\"on_s_m\": 400");
  replace("\"schema_version\": 1", "\"schema_version\": 2");
  replace("\"on_s_m\": 0", "\"on_s_m\": 0, \"on_s_m\": 1");
  invalid(json + "unexpected");
  invalid(std::string(kMaxStrategyFileBytes + 1, ' '));
}
void realCourseTest() {
  Clock clock;
  Output output;
  Store store;
  Recorder recorder;
  Telemetry telemetry;
  Course course(tab5::course_data);
  Settings settings;
  auto origin = course.locate(tab5::course_data.origin, settings.course_corridor_m);
  assert(std::abs(origin.x - tab5::course_data.pixel_matrix[2]) < 1e-9);
  assert(std::abs(origin.y - tab5::course_data.pixel_matrix[5]) < 1e-9);
  assert(course.hasRoute(CourseRoute::First) && course.hasRoute(CourseRoute::Final) &&
         course.hasRoute(CourseRoute::FinishApproach));
  assert(std::abs(course.routeLength(CourseRoute::First) - 2124.583905) < 1e-5);
  assert(std::abs(course.routeLength(CourseRoute::Regular) - 2412.009998) < 1e-5);
  assert(std::abs(course.routeLength(CourseRoute::Final) - 2208.099909) < 1e-5);
  const auto start_marker = course.locateOn(settings.start, 1, CourseRoute::First);
  const auto goal_marker = course.locateOn(settings.goal, 1, CourseRoute::FinishApproach);
  assert(start_marker.on_course && goal_marker.on_course);
  assert(std::abs(start_marker.x - 216.370351) < 0.01 &&
         std::abs(start_marker.y - 380.377619) < 0.01);
  assert(std::abs(goal_marker.x - 106.363411) < 0.01 &&
         std::abs(goal_marker.y - 180.726803) < 0.01);
  assert(course.locate(settings.start, 60).lateral_m > 30);
  assert(course.locate(settings.goal, 60).lateral_m > 20);
  auto distant = course.locate({36.6, 140.3}, 60);
  assert(!distant.on_course && (distant.x > 480 || distant.y < 0));
  {
    Clock probe_clock;
    Output probe_output;
    Store probe_store;
    Recorder probe_recorder;
    Telemetry probe_telemetry;
    Application probe(probe_clock, probe_output, probe_store, probe_recorder,
                      probe_telemetry, course, settings);
    assert(probe.start());
    GpsFix fix;
    fix.valid = true;
    fix.received_ms = probe_clock.time;
    fix.position = course.pointAtOn(1500, CourseRoute::First);
    probe.gps(fix);
    auto position = probe.snapshot();
    assert(position.route_map.on_course &&
           std::abs(position.route_map.s_m - 1500) < 2);
  }
  Application app(clock, output, store, recorder, telemetry, course, settings);
  auto drive = [&](Application &target, Clock &time_source, CourseRoute route, double from,
                   double to) {
    auto feed = [&](double s) {
      time_source.time += 1000;
      GpsFix fix;
      fix.valid = true;
      fix.received_ms = time_source.time;
      fix.position = course.pointAtOn(s, route);
      target.gps(fix);
      target.tick();
    };
    for (double s = from; s < to; s += 20) feed(s);
    feed(to);
  };
  assert(app.start());
  drive(app, clock, CourseRoute::First, 0, course.routeLength(CourseRoute::First));
  assert(app.snapshot().race.lap == 2);
  for (uint8_t lap = 2; lap <= 6; ++lap) {
    drive(app, clock, CourseRoute::Regular, 0, course.routeLength(CourseRoute::Regular));
    assert(app.snapshot().race.lap == lap + 1);
  }
  // The oval comes within 27 m of the goal. Staying on it must not finish.
  drive(app, clock, CourseRoute::Regular, 2030, 2300);
  assert(app.snapshot().race.phase == RacePhase::Measuring && recorder.ends == 0);
  drive(app, clock, CourseRoute::Final, 0, course.routeLength(CourseRoute::Final));
  assert(app.snapshot().race.phase == RacePhase::Finished && recorder.ends == 1);
  assert(app.snapshot().race.total_ms > 6 * settings.min_lap_ms);

  // Settings may move the lap gate beyond the original route endpoint.
  Settings shifted_settings;
  shifted_settings.timing = course.pointAt(100);
  Clock shifted_clock;
  Output shifted_output;
  Store shifted_store;
  Recorder shifted_recorder;
  Telemetry shifted_telemetry;
  Application shifted(shifted_clock, shifted_output, shifted_store, shifted_recorder,
                      shifted_telemetry, course, shifted_settings);
  assert(shifted.start());
  drive(shifted, shifted_clock, CourseRoute::First, 0, course.routeLength(CourseRoute::First));
  assert(shifted.snapshot().race.lap == 1);
  drive(shifted, shifted_clock, CourseRoute::Regular, 0, 80);
  assert(shifted.snapshot().race.lap == 1 && shifted.snapshot().map.on_course);
  drive(shifted, shifted_clock, CourseRoute::Regular, 81, 120);
  assert(shifted.snapshot().race.lap == 2);

  // A finish setting moved along the branch must move the finish gate too.
  Settings moved_goal_settings;
  moved_goal_settings.goal = course.pointAtOn(150, CourseRoute::FinishApproach);
  Clock moved_goal_clock;
  Output moved_goal_output;
  Store moved_goal_store;
  Recorder moved_goal_recorder;
  Telemetry moved_goal_telemetry;
  Application moved_goal(moved_goal_clock, moved_goal_output, moved_goal_store,
                         moved_goal_recorder, moved_goal_telemetry, course, moved_goal_settings);
  assert(moved_goal.start());
  for (int i = 0; i < 6; ++i) {
    moved_goal_clock.time += 11000;
    assert(moved_goal.manualLap());
  }
  const double moved_goal_route_s = course.routeLength(CourseRoute::Final) -
                                     course.routeLength(CourseRoute::FinishApproach) + 150;
  drive(moved_goal, moved_goal_clock, CourseRoute::Final, 0, moved_goal_route_s + 5);
  assert(moved_goal.snapshot().race.phase == RacePhase::Finished && moved_goal_recorder.ends == 1);
}
void combinedLapTest() {
  Fixture f;
  assert(f.app.start());
  f.travel(700, 4450);
  assert(f.app.manualLap());
  assert(f.app.snapshot().race.lap == 2);
  f.travel(4470, 4570);
  assert(f.app.snapshot().race.lap == 2);  // same pass must not add again
  assert(!f.app.manualLap());              // elapsed < dedup window
}
void controlGestureTest() {
  constexpr uintptr_t power = 1, ignition = 2;
  ControlGesture gesture;
  assert(!gesture.sample(false, power));
  assert(!gesture.sample(true, power));
  assert(!gesture.sample(true, power));  // repeated press samples are not extra taps
  assert(gesture.sample(false, power) == power);
  assert(!gesture.sample(false, power));  // repeated release samples are not extra taps

  assert(!gesture.sample(true, power));
  assert(!gesture.sample(true, 0));
  assert(!gesture.sample(true, power));
  assert(!gesture.sample(false, power));  // leaving the control cancels the tap

  assert(!gesture.sample(true, 0));
  assert(!gesture.sample(true, ignition));
  assert(!gesture.sample(false, ignition));  // entering a control mid-gesture is not a tap

  assert(!gesture.sample(true, power));
  assert(!gesture.sample(false, ignition));
  assert(!gesture.sample(true, ignition));
  assert(gesture.sample(false, ignition) == ignition);

  PowerIntent power_intent;
  assert(power_intent.nextTap() == PowerRequest::On);
  power_intent.accepted(PowerRequest::On);
  assert(power_intent.nextTap() == PowerRequest::Off);  // OFF can interrupt pending ON
  power_intent.accepted(PowerRequest::Off);
  assert(power_intent.nextTap() == PowerRequest::None);  // no ON until OFF is confirmed
  power_intent.observe(false);
  assert(power_intent.nextTap() == PowerRequest::On);
  power_intent.accepted(PowerRequest::On);
  power_intent.observe(true);
  assert(power_intent.nextTap() == PowerRequest::Off);
}
int main() {
  engineTest();
  raceTest();
  manualFinishTest();
  gpsRaceTest();
  passageTest();
  gpsOutageAndManualLapTest();
  wheelTest();
  nmeaTest();
  settingsTest();
  presenterTest();
  settingsFormTest();
  gpsSourceSettingsTest();
  strategyTest();
  realCourseTest();
  combinedLapTest();
  controlGestureTest();
  std::cout << "PASS: engine, race, GPS/manual finish, passages, wheel, NMEA, settings, "
               "strategy, MVP/JSON, control gestures\n";
}
