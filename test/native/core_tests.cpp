#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

#include "../../src/adapters/course_data.h"
#include "application/application.h"
#include "application/telemetry_json.h"
#include "domain/nmea.h"
#include "presentation/presenter.h"
#include "presentation/settings_form.h"

using namespace vega;
struct Clock : IClock {
  Millis time = 100000;
  Millis now() const override { return time; }
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
  unsigned begins = 0, samples = 0, ends = 0;
  EndReason reason{};
  Snapshot last{};
  void begin(uint32_t, const Settings &, Millis) override { ++begins; }
  void sample(const Snapshot &s) override {
    ++samples;
    last = s;
  }
  void event(Event, const Snapshot &) override {}
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
  char json[1024];
  assert(telemetryJson(f.app.snapshot(), json, sizeof(json), "pi", "quoted \"note\"") > 0);
  assert(std::strstr(json, "\"speed\":null") && std::strstr(json, "\"latitude\":null"));
  assert(std::strstr(json, "\"average_speed\":null"));
  assert(std::strstr(json, "\\\"note\\\""));
  assert(!telemetryJson(f.app.snapshot(), json, 8));
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
  auto start = course.locate(settings.start, settings.course_corridor_m);
  assert(start.on_course && start.lateral_m > 30 && start.lateral_m < 45);
  assert(course.locate(settings.timing, 60).on_course &&
         course.locate(settings.goal, 60).on_course);
  auto distant = course.locate({36.6, 140.3}, 60);
  assert(!distant.on_course && (distant.x > 480 || distant.y < 0));
  Application app(clock, output, store, recorder, telemetry, course, settings);
  assert(app.start());
  uint8_t last_lap = 1;
  unsigned advances = 0;
  for (double position = start.s_m; position < start.s_m + 8 * course.length(); position += 20) {
    clock.time += 1000;
    GpsFix fix;
    fix.valid = true;
    fix.received_ms = clock.time;
    fix.position = course.pointAt(position);
    app.gps(fix);
    app.tick();
    auto s = app.snapshot();
    if (s.race.lap != last_lap) {
      assert(s.race.lap == last_lap + 1);
      ++advances;
      last_lap = s.race.lap;
    }
    if (s.race.phase == RacePhase::Finished) break;
  }
  assert(advances == 6 && app.snapshot().race.phase == RacePhase::Finished && recorder.ends == 1);
  assert(app.snapshot().race.total_ms > 6 * settings.min_lap_ms);
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
int main() {
  engineTest();
  raceTest();
  gpsRaceTest();
  passageTest();
  wheelTest();
  nmeaTest();
  settingsTest();
  presenterTest();
  settingsFormTest();
  realCourseTest();
  combinedLapTest();
  std::cout << "PASS: engine, race, GPS laps/finish, passages, wheel, NMEA, settings, MVP/JSON\n";
}
