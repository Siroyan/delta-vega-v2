#include "strategy.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>

namespace vega {
namespace {
class Reader {
 public:
  Reader(const char *data, size_t size) : data_(data), size_(size) {}
  const char *error() const { return error_; }
  bool end() { skip(); return pos_ == size_; }
  bool take(char c) {
    skip();
    if (pos_ < size_ && data_[pos_] == c) { ++pos_; return true; }
    return fail("invalid JSON punctuation");
  }
  bool is(char c) { skip(); return pos_ < size_ && data_[pos_] == c; }
  bool word(char *out, size_t capacity) {
    if (!take('"')) return false;
    size_t n = 0;
    while (pos_ < size_ && data_[pos_] != '"') {
      const char c = data_[pos_++];
      const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
      if (!safe || n + 1 >= capacity) return fail("invalid or long identifier");
      out[n++] = c;
    }
    if (pos_ == size_ || !n) return fail("unterminated or empty identifier");
    ++pos_;
    out[n] = 0;
    return true;
  }
  bool number(double &value) {
    skip();
    const size_t start = pos_;
    while (pos_ < size_ && data_[pos_] >= '0' && data_[pos_] <= '9') ++pos_;
    if (pos_ == start) return fail("expected nonnegative number");
    if (pos_ < size_ && data_[pos_] == '.') {
      ++pos_;
      const size_t fractional = pos_;
      while (pos_ < size_ && data_[pos_] >= '0' && data_[pos_] <= '9') ++pos_;
      if (pos_ == fractional) return fail("invalid decimal number");
    }
    if (pos_ - start >= 40) return fail("number too long");
    char token[40];
    std::memcpy(token, data_ + start, pos_ - start);
    token[pos_ - start] = 0;
    value = std::strtod(token, nullptr);
    return std::isfinite(value) || fail("non-finite number");
  }
  bool integer(unsigned &value) {
    double number_value = 0;
    if (!number(number_value) || number_value > std::numeric_limits<unsigned>::max() ||
        std::floor(number_value) != number_value)
      return fail("expected unsigned integer");
    value = static_cast<unsigned>(number_value);
    return true;
  }
  bool fail(const char *message) { if (!error_) error_ = message; return false; }

 private:
  void skip() {
    while (pos_ < size_ && (data_[pos_] == ' ' || data_[pos_] == '\n' ||
                            data_[pos_] == '\r' || data_[pos_] == '\t')) ++pos_;
  }
  const char *data_;
  size_t size_, pos_ = 0;
  const char *error_ = nullptr;
};

bool parseRun(Reader &r, StrategyRun &run) {
  if (!r.take('{')) return false;
  unsigned fields = 0;
  for (bool first = true; !r.is('}'); first = false) {
    if (!first && !r.take(',')) return false;
    char key[24];
    if (!r.word(key, sizeof(key)) || !r.take(':')) return false;
    if (!std::strcmp(key, "on_s_m")) {
      if (fields & 1) return r.fail("duplicate on_s_m");
      fields |= 1;
      if (!r.number(run.on_s_m)) return false;
    } else if (!std::strcmp(key, "off_s_m")) {
      if (fields & 2) return r.fail("duplicate off_s_m");
      fields |= 2;
      if (!r.number(run.off_s_m)) return false;
    } else return r.fail("unknown run field");
  }
  return r.take('}') && (fields == 3 || r.fail("missing run field"));
}

bool parseLap(Reader &r, unsigned &lap_number, StrategyLap &lap) {
  if (!r.take('{')) return false;
  unsigned fields = 0;
  for (bool first = true; !r.is('}'); first = false) {
    if (!first && !r.take(',')) return false;
    char key[24];
    if (!r.word(key, sizeof(key)) || !r.take(':')) return false;
    if (!std::strcmp(key, "lap")) {
      if (fields & 1) return r.fail("duplicate lap");
      fields |= 1;
      if (!r.integer(lap_number)) return false;
    } else if (!std::strcmp(key, "route_id")) {
      if (fields & 2) return r.fail("duplicate route_id");
      fields |= 2;
      char route[24];
      if (!r.word(route, sizeof(route))) return false;
      if (!std::strcmp(route, "first_lap")) lap.route = CourseRoute::First;
      else if (!std::strcmp(route, "regular_lap")) lap.route = CourseRoute::Regular;
      else if (!std::strcmp(route, "final_lap")) lap.route = CourseRoute::Final;
      else return r.fail("unknown route_id");
    } else if (!std::strcmp(key, "runs")) {
      if (fields & 4) return r.fail("duplicate runs");
      fields |= 4;
      if (!r.take('[')) return false;
      for (bool first_run = true; !r.is(']'); first_run = false) {
        if (!first_run && !r.take(',')) return false;
        if (lap.run_count == kMaxStrategyRunsPerLap) return r.fail("too many runs");
        if (!parseRun(r, lap.runs[lap.run_count++])) return false;
      }
      if (!r.take(']')) return false;
    } else return r.fail("unknown lap field");
  }
  return r.take('}') && (fields == 7 || r.fail("missing lap field"));
}
}  // namespace

bool parseStrategy(const char *json, size_t length, const Course &course,
                   const char *expected_course_id, Strategy &out,
                   char *error, size_t error_capacity) {
  auto report = [&](const char *message) {
    if (error && error_capacity) std::snprintf(error, error_capacity, "%s", message);
    return false;
  };
  if (!json || !expected_course_id || !length || length > kMaxStrategyFileBytes)
    return report("strategy file empty or too large");
  Reader r(json, length);
  Strategy candidate{};
  unsigned schema = 0, fields = 0;
  bool seen[kLapCount]{};
  if (!r.take('{')) return report(r.error());
  for (bool first = true; !r.is('}'); first = false) {
    if (!first && !r.take(',')) return report(r.error());
    char key[24];
    if (!r.word(key, sizeof(key)) || !r.take(':')) return report(r.error());
    if (!std::strcmp(key, "schema_version")) {
      if (fields & 1) return report("duplicate schema_version");
      fields |= 1;
      if (!r.integer(schema)) return report(r.error());
    } else if (!std::strcmp(key, "course_id")) {
      if (fields & 2) return report("duplicate course_id");
      fields |= 2;
      if (!r.word(candidate.course_id, sizeof(candidate.course_id))) return report(r.error());
    } else if (!std::strcmp(key, "plan_id")) {
      if (fields & 4) return report("duplicate plan_id");
      fields |= 4;
      if (!r.word(candidate.plan_id, sizeof(candidate.plan_id))) return report(r.error());
    } else if (!std::strcmp(key, "plan_type")) {
      if (fields & 16) return report("duplicate plan_type");
      fields |= 16;
      char type[12];
      if (!r.word(type, sizeof(type))) return report(r.error());
      if (!std::strcmp(type, "demo")) candidate.demo = true;
      else if (!std::strcmp(type, "race")) candidate.demo = false;
      else return report("unknown plan_type");
    } else if (!std::strcmp(key, "laps")) {
      if (fields & 8) return report("duplicate laps");
      fields |= 8;
      if (!r.take('[')) return report(r.error());
      for (bool first_lap = true; !r.is(']'); first_lap = false) {
        if (!first_lap && !r.take(',')) return report(r.error());
        unsigned lap_number = 0;
        StrategyLap lap{};
        if (!parseLap(r, lap_number, lap)) return report(r.error());
        if (lap_number < 1 || lap_number > course.lapCount() || seen[lap_number - 1])
          return report("invalid or duplicate lap number");
        seen[lap_number - 1] = true;
        candidate.laps[lap_number - 1] = lap;
      }
      if (!r.take(']')) return report(r.error());
    } else return report("unknown strategy field");
  }
  if (!r.take('}') || !r.end()) return report("trailing or invalid JSON");
  // The first bundled dummy was installed on development cards before
  // plan_type existed. Accept only that named demo for compatibility.
  if (fields == 15 && !std::strcmp(candidate.plan_id, "dummy-race-001"))
    candidate.demo = true;
  else if (fields != 31)
    return report("missing strategy field");
  if (schema != 1) return report("unsupported schema");
  if (std::strcmp(candidate.course_id, expected_course_id)) return report("course_id mismatch");
  for (size_t i = 0; i < course.lapCount(); ++i) {
    if (!seen[i]) return report("missing lap");
    auto &lap = candidate.laps[i];
    const auto expected_route = i == 0 ? CourseRoute::First
                                : i == course.lapCount() - 1 ? CourseRoute::Final
                                                     : CourseRoute::Regular;
    if (lap.route != expected_route || !course.hasRoute(lap.route))
      return report("route_id mismatch");
    const double length_m = course.routeLength(lap.route);
    if (!std::isfinite(length_m) || length_m <= 0 || !lap.run_count)
      return report("empty or invalid route");
    lap.route_length_m = length_m;
    double previous_off = -1;
    for (size_t j = 0; j < lap.run_count; ++j) {
      const auto &run = lap.runs[j];
      if (run.on_s_m < 0 || run.on_s_m <= previous_off ||
          run.off_s_m <= run.on_s_m || run.off_s_m > length_m)
        return report("run positions out of range or order");
      previous_off = run.off_s_m;
    }
  }
  out = candidate;
  if (error && error_capacity) error[0] = 0;
  return true;
}

NextStrategyCue nextStrategyCue(const StrategyLap &lap, double s_m, double route_length_m) {
  const double current = std::isfinite(s_m) ? std::fmax(0.0, s_m) : 0.0;
  for (size_t i = 0; i < lap.run_count; ++i) {
    if (current <= lap.runs[i].on_s_m)
      return {StrategyCue::On, lap.runs[i].on_s_m - current};
    if (current <= lap.runs[i].off_s_m)
      return {StrategyCue::Off, lap.runs[i].off_s_m - current};
  }
  return {StrategyCue::LapEnd, std::fmax(0.0, route_length_m - current)};
}
}  // namespace vega
