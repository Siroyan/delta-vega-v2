#include "strategy_motion.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace sim {
namespace {
class Reader {
 public:
  Reader(const char *data, size_t size) : data_(data), size_(size) {}
  bool is(char c) { space(); return pos_ < size_ && data_[pos_] == c; }
  bool take(char c) { if (!is(c)) return false; ++pos_; return true; }
  bool end() { space(); return pos_ == size_; }
  bool word(std::string &value) {
    if (!take('"')) return false;
    value.clear();
    while (pos_ < size_ && data_[pos_] != '"') {
      const char c = data_[pos_++];
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') ||
          value.size() >= 47) return false;
      value += c;
    }
    return !value.empty() && take('"');
  }
  bool number(double &value) {
    space();
    const size_t start = pos_;
    while (pos_ < size_ && data_[pos_] >= '0' && data_[pos_] <= '9') ++pos_;
    if (start == pos_) return false;
    if (pos_ < size_ && data_[pos_] == '.') {
      const size_t fractional = ++pos_;
      while (pos_ < size_ && data_[pos_] >= '0' && data_[pos_] <= '9') ++pos_;
      if (fractional == pos_) return false;
    }
    if (pos_ - start >= 40) return false;
    value = std::strtod(std::string(data_ + start, pos_ - start).c_str(), nullptr);
    return std::isfinite(value);
  }
  bool integer(uint32_t &value) {
    double parsed = 0;
    if (!number(parsed) || parsed > UINT32_MAX || parsed != std::floor(parsed)) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
  }
  template <typename F> bool object(F field) {
    if (!take('{')) return false;
    for (bool first = true; !is('}'); first = false) {
      if (!first && !take(',')) return false;
      std::string key;
      if (!word(key) || !take(':') || !field(key)) return false;
    }
    return take('}');
  }
  template <typename F> bool array(F item) {
    if (!take('[')) return false;
    for (bool first = true; !is(']'); first = false) {
      if (!first && !take(',')) return false;
      if (!item()) return false;
    }
    return take(']');
  }

 private:
  void space() {
    while (pos_ < size_ && (data_[pos_] == ' ' || data_[pos_] == '\r' ||
                           data_[pos_] == '\n' || data_[pos_] == '\t')) ++pos_;
  }
  const char *data_;
  size_t size_, pos_ = 0;
};
bool target(Reader &r, uint32_t &seconds) {
  return r.integer(seconds) && seconds >= 1 && seconds <= 86400;
}
constexpr double kEpsilon = 1e-8;
}  // namespace

bool parseDrivingStrategy(const char *json, size_t size, const CourseSpec &course,
                          DrivingStrategy &out, std::string &error) {
  error = "invalid strategy JSON, fields, course, or intervals";
  if (!json || !size || size > kMaxStrategyBytes || course.lap_count < 2) return false;
  Reader r(json, size);
  DrivingStrategy next;
  next.laps.resize(course.lap_count);
  std::vector<bool> seen(course.lap_count, false);
  std::string course_id, type;
  uint32_t schema = 0, fields = 0;
  const bool parsed = r.object([&](const std::string &key) {
    unsigned bit = key == "schema_version" ? 1 : key == "course_id" ? 2 :
                   key == "plan_id" ? 4 : key == "plan_type" ? 8 :
                   key == "total_target_s" ? 16 : key == "laps" ? 32 : 0;
    if (!bit || (fields & bit)) return false;
    fields |= bit;
    if (bit == 1) return r.integer(schema) && (schema == 1 || schema == 2);
    if (bit == 2) return r.word(course_id);
    if (bit == 4) return r.word(next.plan_id) && next.plan_id.size() <= 39;
    if (bit == 8) return r.word(type) && (type == "demo" || type == "race");
    if (bit == 16) return target(r, next.total_target_s);
    return r.array([&]() {
      uint32_t number = 0, lap_fields = 0;
      std::string route;
      StrategyLap lap;
      if (!r.object([&](const std::string &lap_key) {
        unsigned lap_bit = lap_key == "lap" ? 1 : lap_key == "route_id" ? 2 :
                           lap_key == "lap_target_s" ? 4 : lap_key == "runs" ? 8 : 0;
        if (!lap_bit || (lap_fields & lap_bit)) return false;
        lap_fields |= lap_bit;
        if (lap_bit == 1) return r.integer(number);
        if (lap_bit == 2) return r.word(route);
        if (lap_bit == 4) return target(r, lap.target_s);
        return r.array([&]() {
          Run run;
          unsigned run_fields = 0;
          if (!r.object([&](const std::string &run_key) {
            unsigned run_bit = run_key == "on_s_m" ? 1 : run_key == "off_s_m" ? 2 : 0;
            if (!run_bit || (run_fields & run_bit)) return false;
            run_fields |= run_bit;
            return r.number(run_bit == 1 ? run.on_m : run.off_m);
          }) || run_fields != 3) return false;
          lap.runs.push_back(run);
          return true;
        });
      }) || (lap_fields != 11 && lap_fields != 15) ||
          number < 1 || number > course.lap_count || seen[number - 1] || lap.runs.empty())
        return false;
      const char *expected = number == 1 ? "first_lap" : number == course.lap_count ?
                                          "final_lap" : "regular_lap";
      if (route != expected) return false;
      double previous_off = -1;
      for (const auto &run : lap.runs) {
        if (!(run.on_m > previous_off && run.off_m > run.on_m &&
              run.off_m <= course.length(number - 1))) return false;
        previous_off = run.off_m;
      }
      seen[number - 1] = true;
      next.laps[number - 1] = std::move(lap);
      return true;
    });
  });
  const unsigned expected_fields = schema == 2 ? 63 : 47;
  const bool legacy_demo = schema == 1 && fields == 39 && next.plan_id == "dummy-race-001";
  if (!parsed || !r.end() || (fields != expected_fields && !legacy_demo) ||
      course_id != course.course_id) return false;
  for (size_t i = 0; i < seen.size(); ++i)
    if (!seen[i] || (next.laps[i].target_s != 0) != (schema == 2)) return false;
  out = std::move(next);
  error.clear();
  return true;
}

bool validMotionConfig(const MotionConfig &c) {
  return std::isfinite(c.max_speed_kmh) && c.max_speed_kmh >= 1 && c.max_speed_kmh <= 60 &&
         std::isfinite(c.initial_speed_kmh) && c.initial_speed_kmh >= 0 &&
         c.initial_speed_kmh <= c.max_speed_kmh &&
         std::isfinite(c.acceleration_mps2) && c.acceleration_mps2 >= 0.01 && c.acceleration_mps2 <= 5 &&
         std::isfinite(c.coast_deceleration_mps2) && c.coast_deceleration_mps2 >= 0.001 &&
         c.coast_deceleration_mps2 <= 2;
}

void Motion::reset(const CourseSpec &course, const MotionConfig &config,
                   const DrivingStrategy *strategy) {
  course_ = course;
  config_ = config;
  strategy_ = strategy;
  state_ = {};
  state_.speed_mps = (strategy ? config.initial_speed_kmh : config.max_speed_kmh) / 3.6;
}

void Motion::event(MotionEventKind kind, MotionCallback callback, void *context) const {
  if (callback) callback({kind, static_cast<uint8_t>(state_.lap_index + 1), state_.route_s_m,
                          state_.speed_mps * 3.6, state_.elapsed_s, state_.lap_elapsed_s,
                          strategy_ ? strategy_->laps[state_.lap_index].target_s : 0}, context);
}

void Motion::advance(double seconds, MotionCallback callback, void *context) {
  if (!std::isfinite(seconds) || seconds <= 0 || state_.finished || state_.stalled) return;
  while (seconds > kEpsilon && !state_.finished && !state_.stalled) {
    const double route_end = course_.length(state_.lap_index);
    double boundary = route_end;
    bool engine_on = false;
    if (strategy_) {
      for (const auto &run : strategy_->laps[state_.lap_index].runs) {
        if (state_.route_s_m + kEpsilon < run.on_m) { boundary = run.on_m; break; }
        if (state_.route_s_m + kEpsilon < run.off_m) {
          engine_on = true; boundary = run.off_m; break;
        }
      }
    }
    if (engine_on != state_.engine_on) {
      state_.engine_on = engine_on;
      event(engine_on ? MotionEventKind::EngineOn : MotionEventKind::EngineOff, callback, context);
    }
    const double cap = config_.max_speed_kmh / 3.6;
    double acceleration = strategy_ ? (engine_on ? config_.acceleration_mps2 :
                                                   -config_.coast_deceleration_mps2) : 0;
    if (acceleration > 0 && state_.speed_mps >= cap - kEpsilon) acceleration = 0;
    if (state_.speed_mps <= kEpsilon && acceleration <= 0) {
      state_.speed_mps = 0;
      state_.stalled = true;
      event(MotionEventKind::Stalled, callback, context);
      break;
    }
    double step_s = seconds;
    if (acceleration > 0) step_s = std::min(step_s, (cap - state_.speed_mps) / acceleration);
    if (acceleration < 0) step_s = std::min(step_s, -state_.speed_mps / acceleration);
    const double distance = std::max(0.0, boundary - state_.route_s_m);
    const double predicted = state_.speed_mps * step_s + 0.5 * acceleration * step_s * step_s;
    const bool reached = predicted + kEpsilon >= distance;
    if (reached) {
      const double final_speed = std::sqrt(std::max(0.0, state_.speed_mps * state_.speed_mps +
                                                        2 * acceleration * distance));
      step_s = distance > 0 ? 2 * distance / (state_.speed_mps + final_speed) : 0;
    }
    const double travelled = reached ? distance : predicted;
    state_.route_s_m += travelled;
    state_.total_m += travelled;
    state_.speed_mps = std::clamp(state_.speed_mps + acceleration * step_s, 0.0, cap);
    state_.elapsed_s += step_s;
    state_.lap_elapsed_s += step_s;
    seconds = std::max(0.0, seconds - step_s);
    if (reached) state_.route_s_m = boundary;
    if (state_.route_s_m + kEpsilon >= route_end) {
      if (state_.lap_index + 1 >= course_.lap_count) {
        state_.finished = true;
        state_.speed_mps = 0;
        event(MotionEventKind::Goal, callback, context);
      } else {
        event(MotionEventKind::Lap, callback, context);
        ++state_.lap_index;
        state_.route_s_m = 0;
        state_.lap_elapsed_s = 0;
      }
    }
  }
}
}  // namespace sim
