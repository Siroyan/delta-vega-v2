#include "course.h"

#include <algorithm>
#include <limits>

namespace vega {
MapPosition Course::locate(GeoPoint p, double corridor) const {
  MapPosition r;
  if (!validGeo(p) || data_.count < 2 || data_.length <= 0) return r;
  double east = (p.longitude - data_.origin.longitude) * data_.east_per_degree;
  double north = (p.latitude - data_.origin.latitude) * data_.north_per_degree;
  const auto &m = data_.pixel_matrix;
  // Render the real coordinate, rather than silently snapping a distant fix onto the course.
  r.x = m[0] * east + m[1] * north + m[2];
  r.y = m[3] * east + m[4] * north + m[5];
  r.lateral_m = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < data_.count; ++i) {
    const auto &a = data_.points[i], &b = data_.points[(i + 1) % data_.count];
    double dx = b.east - a.east, dy = b.north - a.north, squared = dx * dx + dy * dy;
    if (squared == 0) continue;
    double t = std::clamp(((east - a.east) * dx + (north - a.north) * dy) / squared, 0.0, 1.0);
    double distance = std::hypot(east - a.east - t * dx, north - a.north - t * dy);
    if (distance < r.lateral_m) {
      r.lateral_m = distance;
      double end = i + 1 < data_.count ? b.s : data_.length;
      r.s_m = a.s + t * (end - a.s);
    }
  }
  r.on_course = r.lateral_m <= corridor;
  return r;
}
GeoPoint Course::pointAt(double s) const {
  s = std::fmod(s, data_.length);
  if (s < 0) s += data_.length;
  for (size_t i = 0; i < data_.count; ++i) {
    const auto &a = data_.points[i], &b = data_.points[(i + 1) % data_.count];
    double end = i + 1 < data_.count ? b.s : data_.length;
    if (s >= a.s && s <= end) {
      double t = (s - a.s) / (end - a.s);
      return {data_.origin.latitude + (a.north + t * (b.north - a.north)) / data_.north_per_degree,
              data_.origin.longitude + (a.east + t * (b.east - a.east)) / data_.east_per_degree};
    }
  }
  return data_.origin;
}
double Course::forwardDelta(double from, double to) const {
  double delta = to - from;
  if (delta > data_.length / 2) delta -= data_.length;
  if (delta < -data_.length / 2) delta += data_.length;
  return delta;
}
bool PassageDetector::update(const MapPosition &p, Millis now, double gate, double min_progress,
                             const Course &course, const Settings &s) {
  if (!p.on_course) {
    reset();
    return false;
  }
  if (!initialized_ || now < previous_time_ || now - previous_time_ > s.gps_stale_ms) {
    initialized_ = true;
    previous_s_ = p.s_m;
    previous_time_ = now;
    progress_ = 0;
    return false;
  }
  double delta = course.forwardDelta(previous_s_, p.s_m);
  double to_gate = course.forwardDelta(previous_s_, gate);
  previous_s_ = p.s_m;
  previous_time_ = now;
  if (std::abs(delta) > s.max_gps_step_m) {
    progress_ = 0;
    return false;
  }
  progress_ = std::max(0.0, progress_ + delta);
  if (delta > 0 && to_gate > 0 && to_gate <= delta && progress_ >= min_progress) {
    progress_ = 0;
    return true;
  }
  return false;
}
}  // namespace vega
