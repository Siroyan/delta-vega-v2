#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "course_data.h"
#include "motion_settings.h"
#include "sim_display.h"
#include "strategy_storage.h"

namespace {

constexpr int kGpsRxPin = 2;       // Port.CUSTOM yellow; optional Tab5 TX
constexpr int kGpsTxPin = 1;       // Port.CUSTOM white -> Tab5 Port.A G54
constexpr int kWheelPin = 5;       // Bottom G5 -> Tab5 M5Bus G16
constexpr uint32_t kRmcPeriodMs = 200;
constexpr uint32_t kGgaPeriodMs = 1000;
constexpr uint32_t kPulseWidthUs = 5000;
constexpr double kPi = 3.14159265358979323846;

enum class Mode { Idle, Running, Paused, Finished, Stopped, Stalled };
sim::Motion motion;
sim::StrategyStorage driving_strategy;
sim::MotionConfig driving_config;

struct State {
  uint8_t course_index = 0;
  Mode mode = Mode::Idle;
  double speed_kmh = 12;
  double wheel_circumference_m = 1.03;
  uint32_t pulses_per_revolution = 1;
  uint64_t pulse_count = 0;
  uint32_t pulse_missed = 0;
  double next_pulse_m = 1.03;
  bool pulse_low = false;
  uint64_t release_pulse_us = 0;
  uint64_t last_step_us = 0;
  uint32_t next_rmc_ms = 0;
  uint32_t next_gga_ms = 0;
  uint32_t next_report_ms = 0;
  double noise_sigma_m = 0;
  double bias_e_m = 0, bias_n_m = 0;
  double spike_e_m = 0, spike_n_m = 0;
  bool spike_pending = false;
  uint32_t dropout_until_ms = 0;
  uint32_t invalid_until_ms = 0;
  uint32_t bad_checksums = 0;
  bool echo_nmea = false;
  uint32_t random_state = 1;
} state;
Preferences preferences;
bool preferences_ready = false;

void saveCourse() {
  if (preferences_ready) preferences.putUChar("course", state.course_index);
}

void saveWheel() {
  if (preferences_ready) {
    preferences.putFloat("wheel", state.wheel_circumference_m);
    preferences.putUInt("ppr", state.pulses_per_revolution);
  }
}

void saveMotionSettings() {
  if (preferences_ready && !sim::saveMotionSettings(preferences, driving_config))
    Serial.println("[SIM] motion settings save failed; current settings are not persistent");
}

void loadSettings() {
  preferences_ready = preferences.begin("atom-sim", false);
  if (!preferences_ready) {
    Serial.println("[SIM] NVS unavailable; defaults will be used");
    return;
  }
  const uint8_t stored_course = preferences.getUChar("course", 0);
  const float stored_wheel = preferences.getFloat("wheel", 1.03);
  const uint32_t stored_ppr = preferences.getUInt("ppr", 1);
  if (stored_course < sim::course_count) state.course_index = stored_course;
  if (isfinite(stored_wheel) && stored_wheel >= 0.1 && stored_wheel <= 10)
    state.wheel_circumference_m = stored_wheel;
  if (stored_ppr >= 1 && stored_ppr <= 100)
    state.pulses_per_revolution = stored_ppr;
  driving_config.max_speed_kmh = state.speed_kmh;
  const auto loaded = sim::loadMotionSettings(preferences, driving_config);
  if (loaded == sim::MotionSettingsLoad::Invalid)
    Serial.println("[SIM] invalid saved motion settings; defaults will be used");
  else if (loaded == sim::MotionSettingsLoad::Legacy)
    saveMotionSettings();
  state.speed_kmh = driving_config.max_speed_kmh;
}

const sim::Course &course() { return sim::courses[state.course_index]; }
sim::CourseSpec courseSpec() {
  return {course().strategy_course_id, course().lap_count, course().first.length_m,
          course().regular.length_m, course().final.length_m};
}

const sim::Route &route() {
  if (motion.state().lap_index == 0) return course().first;
  if (motion.state().lap_index + 1 == course().lap_count) return course().final;
  return course().regular;
}

const char *modeName() {
  switch (state.mode) {
    case Mode::Idle: return "idle";
    case Mode::Running: return "running";
    case Mode::Paused: return "paused";
    case Mode::Finished: return "finished";
    case Mode::Stopped: return "stopped";
    case Mode::Stalled: return "stalled";
  }
  return "?";
}

double pulseDistance() {
  return state.wheel_circumference_m / state.pulses_per_revolution;
}

void releasePulse() {
  digitalWrite(kWheelPin, HIGH); // HIGH is high impedance in open-drain mode.
  state.pulse_low = false;
}

void resetRace() {
  releasePulse();
  state.mode = Mode::Idle;
  driving_config.max_speed_kmh = state.speed_kmh;
  motion.reset(courseSpec(), driving_config, driving_strategy.plan());
  state.pulse_count = 0;
  state.pulse_missed = 0;
  state.next_pulse_m = pulseDistance();
  state.last_step_us = esp_timer_get_time();
  Serial.println("[SIM] race reset at START");
}

void startRace() {
  if (state.mode != Mode::Idle) return;
  state.mode = Mode::Running;
  state.last_step_us = esp_timer_get_time();
  Serial.println("[SIM] START");
}

void pauseRace() {
  if (state.mode != Mode::Running) return;
  state.mode = Mode::Paused;
  releasePulse();
  Serial.println("[SIM] PAUSE");
}

void resumeRace() {
  if (state.mode != Mode::Paused) return;
  state.mode = Mode::Running;
  state.last_step_us = esp_timer_get_time();
  Serial.println("[SIM] RESUME");
}

void stopRace() {
  state.mode = Mode::Stopped;
  releasePulse();
  Serial.println("[SIM] STOP");
}

void selectNextCourse() {
  driving_strategy.clear();
  if (preferences_ready) preferences.remove("strategy");
  state.course_index = (state.course_index + 1) % sim::course_count;
  saveCourse();
  resetRace();
  Serial.printf("[SIM] selected course=%s\n", course().id);
}

void pollButton(uint32_t now_ms) {
  static uint32_t last_poll_ms = 0;
  static uint32_t pressed_at_ms = 0;
  static bool was_pressed = false;
  if (now_ms - last_poll_ms < 10) return;
  last_poll_ms = now_ms;
  M5.update();
  const bool pressed = M5.BtnA.isPressed();
  if (pressed && !was_pressed) pressed_at_ms = now_ms;
  if (!pressed && was_pressed) {
    const uint32_t held_ms = now_ms - pressed_at_ms;
    if (held_ms >= 30) {
      if (state.mode == Mode::Idle) {
        if (held_ms >= 1200) selectNextCourse();
        else startRace();
      } else if (state.mode == Mode::Running) {
        if (held_ms >= 2000) stopRace();
        else pauseRace();
      } else if (state.mode == Mode::Paused) {
        if (held_ms >= 2000) resetRace();
        else resumeRace();
      } else if (state.mode == Mode::Finished || state.mode == Mode::Stopped || state.mode == Mode::Stalled) {
        resetRace();
      }
    }
  }
  was_pressed = pressed;
}

void motionEvent(const sim::MotionEvent &event, void *) {
  const char *kind = event.kind == sim::MotionEventKind::EngineOn ? "ENGINE ON" :
                     event.kind == sim::MotionEventKind::EngineOff ? "ENGINE OFF" :
                     event.kind == sim::MotionEventKind::Lap ? "LAP END" :
                     event.kind == sim::MotionEventKind::Goal ? "GOAL" : "STALLED";
  Serial.printf("[SIM] %s course=%s lap=%u s=%.3f speed=%.2f elapsed_s=%.3f "
                "lap_elapsed_s=%.3f target_s=%lu\n", kind, course().id, event.lap,
                event.s_m, event.speed_kmh, event.elapsed_s, event.lap_elapsed_s,
                static_cast<unsigned long>(event.target_s));
}

void advanceMotion(uint64_t now_us) {
  const double dt = static_cast<double>(now_us - state.last_step_us) / 1000000.0;
  state.last_step_us = now_us;
  if (state.mode != Mode::Running || dt <= 0) return;
  if (dt > 0.25) Serial.printf("[SIM] scheduler delay %.0f ms\n", dt * 1000);
  motion.advance(dt, motionEvent);
  if (motion.state().finished || motion.state().stalled) {
    state.mode = motion.state().finished ? Mode::Finished : Mode::Stalled;
    releasePulse();
  }
}

void updateWheel(uint64_t now_us) {
  if (state.pulse_low && now_us >= state.release_pulse_us) releasePulse();
  if (state.mode != Mode::Running || state.pulse_low || motion.state().total_m < state.next_pulse_m)
    return;
  const double interval = pulseDistance();
  // An overloaded loop must not produce a physically impossible catch-up burst.
  while (motion.state().total_m >= state.next_pulse_m + interval) {
    state.next_pulse_m += interval;
    ++state.pulse_missed;
  }
  digitalWrite(kWheelPin, LOW);
  state.pulse_low = true;
  state.release_pulse_us = now_us + kPulseWidthUs;
  ++state.pulse_count;
  state.next_pulse_m += interval;
}

struct Position { double east, north, heading; };

Position position() {
  const sim::Route &r = route();
  uint16_t i = 1;
  while (i + 1 < r.count && r.points[i].s_m < motion.state().route_s_m) ++i;
  const sim::Point &a = r.points[i - 1];
  const sim::Point &b = r.points[i];
  const double span = b.s_m - a.s_m;
  const double t = span > 0 ? fmax(0.0, fmin(1.0, (motion.state().route_s_m - a.s_m) / span)) : 0;
  const double east = a.east_m + (b.east_m - a.east_m) * t;
  const double north = a.north_m + (b.north_m - a.north_m) * t;
  const double heading = fmod(atan2(b.east_m - a.east_m, b.north_m - a.north_m) *
                                   180.0 / kPi + 360.0, 360.0);
  return {east, north, heading};
}

uint32_t randomWord() {
  uint32_t x = state.random_state;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return state.random_state = x;
}

double unitRandom() { return (randomWord() & 0xffffff) / 16777216.0; }

double normalRandom() {
  // Sum of uniforms: deterministic, approximately normal, no expensive transcendentals.
  double sum = -6.0;
  for (int i = 0; i < 12; ++i) sum += unitRandom();
  return sum;
}

void nmeaCoord(double degrees, bool latitude, char *field, size_t size, char &hemisphere) {
  hemisphere = degrees < 0 ? (latitude ? 'S' : 'W') : (latitude ? 'N' : 'E');
  degrees = fabs(degrees);
  int whole = static_cast<int>(degrees);
  long minute_ticks = lround((degrees - whole) * 600000.0); // 1/10000 minute
  if (minute_ticks >= 600000) { ++whole; minute_ticks = 0; }
  if (latitude)
    snprintf(field, size, "%02d%02ld.%04ld", whole, minute_ticks / 10000,
             minute_ticks % 10000);
  else
    snprintf(field, size, "%03d%02ld.%04ld", whole, minute_ticks / 10000,
             minute_ticks % 10000);
}

void sendNmea(const char *body) {
  uint8_t checksum = 0;
  for (const char *p = body; *p; ++p) checksum ^= static_cast<uint8_t>(*p);
  if (state.bad_checksums) {
    checksum ^= 0x55;
    --state.bad_checksums;
  }
  char sentence[180];
  snprintf(sentence, sizeof(sentence), "$%s*%02X\r\n", body, checksum);
  Serial1.print(sentence);
  if (state.echo_nmea) Serial.printf("[NMEA] %s", sentence);
}

bool activeUntil(uint32_t until, uint32_t now) {
  return until && static_cast<int32_t>(until - now) > 0;
}

const char *gpsStatus(uint32_t now_ms) {
  if (activeUntil(state.dropout_until_ms, now_ms)) return "GPS DROP";
  if (activeUntil(state.invalid_until_ms, now_ms)) return "GPS NO FIX";
  if (state.bad_checksums) return "GPS BAD CS";
  if (state.noise_sigma_m || state.bias_e_m || state.bias_n_m || state.spike_pending)
    return "GPS NOISE";
  return "GPS OK";
}

double reportedSpeedKmh() {
  return state.mode == Mode::Running || state.mode == Mode::Idle ? motion.state().speed_mps * 3.6 : 0;
}

void updateDisplay(uint32_t now_ms) {
  sim_display::Mode mode = sim_display::Mode::Idle;
  switch (state.mode) {
    case Mode::Idle: mode = sim_display::Mode::Idle; break;
    case Mode::Running: mode = sim_display::Mode::Running; break;
    case Mode::Paused: mode = sim_display::Mode::Paused; break;
    case Mode::Finished: mode = sim_display::Mode::Finished; break;
    case Mode::Stopped: mode = sim_display::Mode::Stopped; break;
    case Mode::Stalled: mode = sim_display::Mode::Stalled; break;
  }
  const sim_display::Snapshot snapshot{
      course().short_name, mode, static_cast<uint8_t>(motion.state().lap_index + 1),
      course().lap_count, static_cast<float>(reportedSpeedKmh()),
      static_cast<float>(motion.state().total_m),
      static_cast<float>(motion.state().route_s_m / route().length_m), state.pulse_count,
      state.pulse_missed, gpsStatus(now_ms), driving_strategy.plan() != nullptr,
      state.mode == Mode::Running && motion.state().engine_on};
  sim_display::update(snapshot, now_ms);
}

void sendFix(bool gga, uint32_t now_ms) {
  if (activeUntil(state.dropout_until_ms, now_ms)) return;
  Position p = position();
  p.east += state.bias_e_m + state.noise_sigma_m * normalRandom();
  p.north += state.bias_n_m + state.noise_sigma_m * normalRandom();
  if (state.spike_pending) {
    p.east += state.spike_e_m;
    p.north += state.spike_n_m;
    // RMC and GGA are sampled independently. The spike affects one sentence.
    state.spike_pending = false;
  }
  const double latitude = course().origin_lat + p.north / course().north_per_lat;
  const double longitude = course().origin_lon + p.east / course().east_per_lon;
  char lat[20], lon[20], body[160];
  char ns, ew;
  nmeaCoord(latitude, true, lat, sizeof(lat), ns);
  nmeaCoord(longitude, false, lon, sizeof(lon), ew);
  // Fixed simulated UTC origin. Time advances with uptime but does not require Wi-Fi.
  time_t seconds = static_cast<time_t>(1767268800) + now_ms / 1000;
  tm utc{};
  gmtime_r(&seconds, &utc);
  char hhmmss[16], ddmmyy[16];
  snprintf(hhmmss, sizeof(hhmmss), "%02d%02d%02d.%02u", utc.tm_hour,
           utc.tm_min, utc.tm_sec, (now_ms % 1000) / 10);
  snprintf(ddmmyy, sizeof(ddmmyy), "%02d%02d%02d", utc.tm_mday,
           utc.tm_mon + 1, (utc.tm_year + 1900) % 100);
  const bool valid = !activeUntil(state.invalid_until_ms, now_ms);
  const double knots = state.mode == Mode::Running ? motion.state().speed_mps * 3.6 / 1.852 : 0;
  if (gga) {
    snprintf(body, sizeof(body), "GPGGA,%s,%s,%c,%s,%c,%u,%02u,0.9,40.0,M,0.0,M,,",
             hhmmss, lat, ns, lon, ew, valid ? 1u : 0u, valid ? 10u : 0u);
  } else {
    snprintf(body, sizeof(body), "GPRMC,%s,%c,%s,%c,%s,%c,%.1f,%.1f,%s,,,A",
             hhmmss, valid ? 'A' : 'V', lat, ns, lon, ew, knots, p.heading, ddmmyy);
  }
  sendNmea(body);
}

void printStatus() {
  Position p = position();
  const double lat = course().origin_lat + p.north / course().north_per_lat;
  const double lon = course().origin_lon + p.east / course().east_per_lon;
  Serial.printf("[SIM] mode=%s course=%s lap=%u/%u route=%.1f/%.1f m total=%.1f m "
                "speed=%.1f km/h pulses=%llu missed=%u lat=%.7f lon=%.7f "
                "noise=%.1f m bias=(%.1f,%.1f) motion=%s engine=%u plan=%s "
                "sha256=%s max_speed=%.2f initial_speed=%.2f accel=%.4f coast=%.4f elapsed_s=%.3f protocol=2\n",
                modeName(), course().id, motion.state().lap_index + 1, course().lap_count,
                motion.state().route_s_m, route().length_m, motion.state().total_m, reportedSpeedKmh(),
                static_cast<unsigned long long>(state.pulse_count), state.pulse_missed,
                lat, lon, state.noise_sigma_m, state.bias_e_m, state.bias_n_m,
                driving_strategy.plan() ? "strategy" : "constant", state.mode == Mode::Running && motion.state().engine_on,
                driving_strategy.plan() ? driving_strategy.plan()->plan_id.c_str() : "none",
                driving_strategy.plan() ? driving_strategy.sha256().c_str() : "none",
                state.speed_kmh, driving_config.initial_speed_kmh,
                driving_config.acceleration_mps2, driving_config.coast_deceleration_mps2,
                motion.state().elapsed_s);
}

void printHelp() {
  Serial.println("[SIM] commands: help | status | courses | course <id|index> | "
                 "start | pause | resume | stop | reset");
  Serial.println("[SIM] speed <km/h 1..60> | wheel <circumference_m> <pulses_per_rev> | "
                 "seed <nonzero_uint32>");
  Serial.println("[SIM] noise <sigma_m> | bias <east_m> <north_m> | "
                 "spike <east_m> <north_m>");
  Serial.println("[SIM] dropout <ms> | invalid <ms> | corrupt <sentence_count> "
                 "(0 clears each fault)");
  Serial.println("[SIM] nmea on|off  (mirror generated sentences to USB serial)");
  Serial.println("[SIM] strategies | strategy <filename.json|none> | "
                 "dynamics <accel_mps2> <coast_mps2> <initial_kmh>");
  Serial.println("[SIM] plan-begin <bytes> <sha256> | plan-chunk <hex> | plan-end");
}

void planAccepted() {
  if (preferences_ready) preferences.putString("strategy", driving_strategy.filename().c_str());
  resetRace();
  Serial.printf("[SIM PLAN] ready=1 plan=%s sha256=%s\n",
                driving_strategy.plan()->plan_id.c_str(), driving_strategy.sha256().c_str());
}

void command(char *line) {
  char *save = nullptr;
  char *verb = strtok_r(line, " \t", &save);
  if (!verb) return;
  char *arg1 = strtok_r(nullptr, " \t", &save);
  char *arg2 = strtok_r(nullptr, " \t", &save);
  char *arg3 = strtok_r(nullptr, " \t", &save);
  const bool stopped = state.mode != Mode::Running && state.mode != Mode::Paused;
  std::string error;
  if (!strcmp(verb, "help")) printHelp();
  else if (!strcmp(verb, "status")) printStatus();
  else if (!strcmp(verb, "courses")) {
    for (uint8_t i = 0; i < sim::course_count; ++i)
      Serial.printf("[SIM] %u: %s (%u laps)\n", i, sim::courses[i].id,
                    sim::courses[i].lap_count);
  } else if (!strcmp(verb, "strategies")) {
    File folder = LittleFS.open("/strategies");
    for (File file = folder.openNextFile(); file; file = folder.openNextFile())
      Serial.printf("[SIM PLAN] file=%s\n", file.name());
  } else if (!strcmp(verb, "strategy") && arg1 && stopped) {
    if (!strcmp(arg1, "none")) {
      driving_strategy.clear();
      if (preferences_ready) preferences.remove("strategy");
      resetRace();
      Serial.println("[SIM PLAN] ready=1 plan=none sha256=none");
    } else if (driving_strategy.load(arg1, courseSpec(), error)) planAccepted();
    else Serial.printf("[SIM PLAN] error=%s\n", error.c_str());
  } else if (!strcmp(verb, "plan-begin") && arg1 && arg2 && stopped) {
    char *end = nullptr;
    const unsigned long bytes = strtoul(arg1, &end, 10);
    if (*end || !driving_strategy.beginUpload(bytes, arg2, error))
      Serial.printf("[SIM PLAN] error=%s\n", error.empty() ? "invalid size" : error.c_str());
    else Serial.println("[SIM PLAN] offset=0");
  } else if (!strcmp(verb, "plan-chunk") && arg1 && stopped) {
    if (!driving_strategy.chunk(arg1, error)) Serial.printf("[SIM PLAN] error=%s\n", error.c_str());
    else Serial.printf("[SIM PLAN] offset=%u\n", static_cast<unsigned>(driving_strategy.received()));
  } else if (!strcmp(verb, "plan-end") && stopped) {
    if (driving_strategy.finishUpload(courseSpec(), error)) planAccepted();
    else Serial.printf("[SIM PLAN] error=%s\n", error.c_str());
  } else if (!strcmp(verb, "dynamics") && arg1 && arg2 && arg3 && stopped) {
    sim::MotionConfig replacement = driving_config;
    char *end1 = nullptr, *end2 = nullptr, *end3 = nullptr;
    replacement.acceleration_mps2 = strtod(arg1, &end1);
    replacement.coast_deceleration_mps2 = strtod(arg2, &end2);
    replacement.initial_speed_kmh = strtod(arg3, &end3);
    replacement.max_speed_kmh = state.speed_kmh;
    if (*end1 || *end2 || *end3 || !sim::validMotionConfig(replacement))
      Serial.println("[SIM PLAN] error=invalid dynamics parameters");
    else {
      driving_config = replacement;
      saveMotionSettings();
      resetRace();
      Serial.println("[SIM PLAN] dynamics=ready");
    }
  } else if (!strcmp(verb, "course") && arg1 && stopped) {
    for (uint8_t i = 0; i < sim::course_count; ++i) {
      if (!strcmp(arg1, sim::courses[i].id) ||
          (strlen(arg1) == 1 && arg1[0] == '0' + i)) {
        state.course_index = i;
        driving_strategy.clear();
        if (preferences_ready) preferences.remove("strategy");
        saveCourse();
        resetRace();
        printStatus();
        return;
      }
    }
    Serial.println("[SIM] unknown course; use courses");
  } else if (!strcmp(verb, "start") && state.mode == Mode::Idle) {
    startRace();
  } else if (!strcmp(verb, "pause") && state.mode == Mode::Running) {
    pauseRace();
  } else if (!strcmp(verb, "resume") && state.mode == Mode::Paused) {
    resumeRace();
  } else if (!strcmp(verb, "stop")) {
    stopRace();
  } else if (!strcmp(verb, "reset")) {
    resetRace();
  } else if (!strcmp(verb, "speed") && arg1 && stopped) {
    const double value = atof(arg1);
    if (value >= 1 && value <= 60) {
      state.speed_kmh = value;
      driving_config.max_speed_kmh = value;
      if (driving_config.initial_speed_kmh > value) driving_config.initial_speed_kmh = value;
      saveMotionSettings();
      Serial.printf("[SIM] speed=%.1f km/h\n", value);
      resetRace();
    } else Serial.println("[SIM] speed must be 1..60 km/h");
  } else if (!strcmp(verb, "wheel") && arg1 && arg2 && state.mode != Mode::Running) {
    const double circumference = atof(arg1);
    const long ppr = strtol(arg2, nullptr, 10);
    if (circumference >= 0.1 && circumference <= 10 && ppr >= 1 && ppr <= 100) {
      state.wheel_circumference_m = circumference;
      state.pulses_per_revolution = ppr;
      state.next_pulse_m = motion.state().total_m + pulseDistance();
      saveWheel();
      Serial.printf("[SIM] wheel=%.3f m x %ld pulses/rev\n", circumference, ppr);
    } else Serial.println("[SIM] wheel range: 0.1..10 m, 1..100 pulses/rev");
  } else if (!strcmp(verb, "seed") && arg1) {
    const unsigned long value = strtoul(arg1, nullptr, 10);
    if (value) { state.random_state = value; Serial.printf("[SIM] seed=%lu\n", value); }
    else Serial.println("[SIM] seed must be nonzero");
  } else if (!strcmp(verb, "noise") && arg1) {
    const double value = atof(arg1);
    if (value >= 0 && value <= 1000) {
      state.noise_sigma_m = value;
      Serial.printf("[SIM] GPS noise sigma=%.1f m\n", value);
    } else Serial.println("[SIM] noise range: 0..1000 m");
  } else if (!strcmp(verb, "bias") && arg1 && arg2) {
    const double east = atof(arg1), north = atof(arg2);
    if (fabs(east) <= 10000 && fabs(north) <= 10000) {
      state.bias_e_m = east; state.bias_n_m = north;
      Serial.printf("[SIM] GPS bias=(%.1f,%.1f) m\n", east, north);
    } else Serial.println("[SIM] bias range: +/-10000 m");
  } else if (!strcmp(verb, "spike") && arg1 && arg2) {
    const double east = atof(arg1), north = atof(arg2);
    if (fabs(east) <= 10000 && fabs(north) <= 10000) {
      state.spike_e_m = east; state.spike_n_m = north;
      state.spike_pending = true;
      Serial.printf("[SIM] next GPS sentence spike=(%.1f,%.1f) m\n", east, north);
    } else Serial.println("[SIM] spike range: +/-10000 m");
  } else if ((!strcmp(verb, "dropout") || !strcmp(verb, "invalid")) && arg1) {
    const unsigned long ms = strtoul(arg1, nullptr, 10);
    if (ms <= 3600000) {
      uint32_t &until = !strcmp(verb, "dropout") ? state.dropout_until_ms : state.invalid_until_ms;
      until = ms ? millis() + ms : 0;
      Serial.printf("[SIM] %s=%lu ms\n", verb, ms);
    } else Serial.println("[SIM] duration max 3600000 ms");
  } else if (!strcmp(verb, "corrupt") && arg1) {
    const unsigned long count = strtoul(arg1, nullptr, 10);
    if (count <= 10000) {
      state.bad_checksums = count;
      Serial.printf("[SIM] corrupt next %lu sentences\n", count);
    } else Serial.println("[SIM] corrupt max 10000 sentences");
  } else if (!strcmp(verb, "nmea") && arg1 &&
             (!strcmp(arg1, "on") || !strcmp(arg1, "off"))) {
    state.echo_nmea = !strcmp(arg1, "on");
    Serial.printf("[SIM] NMEA echo %s\n", arg1);
  } else Serial.println("[SIM] invalid command or state; use help");
}

void readCommands() {
  static char line[128];
  static size_t used = 0;
  while (Serial.available()) {
    const int ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (used) { line[used] = '\0'; command(line); used = 0; }
    } else if (ch >= 32 && ch < 127) {
      if (used + 1 < sizeof(line)) line[used++] = ch;
      else { used = 0; Serial.println("[SIM] command too long"); }
    }
  }
}

} // namespace

void setup() {
  digitalWrite(kWheelPin, HIGH);
  pinMode(kWheelPin, OUTPUT_OPEN_DRAIN);
  digitalWrite(kWheelPin, HIGH);
  Serial.begin(115200);
  sim_display::begin();
  Serial.println("[SIM] opening course JSON from LittleFS");
  if (!sim::loadCoursesFromFs()) {
    Serial.println("[SIM] course data unavailable; check LittleFS upload");
    M5.Display.fillScreen(0x0000);
    M5.Display.setTextColor(0xFFFF);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("COURSE DATA", 64, 53);
    M5.Display.drawString("UNAVAILABLE", 64, 73);
    for (;;) {
      Serial.println("[SIM] course data unavailable; run uploadfs, then reboot");
      delay(3000); // Keep the wheel output released; never drive without a course.
    }
  }
  Serial.printf("[SIM] course JSON ready after %lu ms\n", millis());
  loadSettings();
  if (preferences_ready) {
    const String selected = preferences.getString("strategy", "");
    std::string error;
    if (selected.length() && !driving_strategy.load(selected.c_str(), courseSpec(), error)) {
      Serial.printf("[SIM PLAN] restore failed: %s; strategy cleared\n", error.c_str());
      preferences.remove("strategy");
    }
  }
  // HardwareSerial otherwise writes into only the UART FIFO and can block long
  // enough at 9600 bps to miss wheel pulse deadlines.
  Serial1.setTxBufferSize(512);
  Serial1.begin(9600, SERIAL_8N1, kGpsRxPin, kGpsTxPin);
  resetRace();
  state.next_rmc_ms = millis();
  state.next_gga_ms = millis();
  state.next_report_ms = millis() + 5000;
  Serial.println("[SIM] AtomS3 GPS + wheel simulator ready; type help");
  printStatus();
}

void loop() {
  readCommands();
  const uint32_t now_ms = millis();
  pollButton(now_ms);
  const uint64_t now_us = esp_timer_get_time();
  advanceMotion(now_us);
  updateWheel(now_us);
  while (Serial1.available()) Serial1.read(); // Tab5 receiver configuration is optional.
  if (static_cast<int32_t>(now_ms - state.next_rmc_ms) >= 0) {
    sendFix(false, now_ms);
    state.next_rmc_ms = now_ms + kRmcPeriodMs;
  }
  if (static_cast<int32_t>(now_ms - state.next_gga_ms) >= 0) {
    sendFix(true, now_ms);
    state.next_gga_ms = now_ms + kGgaPeriodMs;
  }
  if (static_cast<int32_t>(now_ms - state.next_report_ms) >= 0) {
    printStatus();
    state.next_report_ms = now_ms + 5000;
  }
  updateDisplay(now_ms);
  delay(1);
}
