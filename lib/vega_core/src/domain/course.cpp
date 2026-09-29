#include "course.h"

#include <algorithm>
#include <limits>

namespace vega {
CoursePath Course::path(CourseRoute route) const {
  const auto &configured = data_.routes[static_cast<size_t>(route)];
  if (configured.points && configured.count >= 2 && configured.length > 0) return configured;
  return {data_.points, data_.count, data_.length, true};
}
bool Course::hasRoute(CourseRoute route) const {
  return data_.routes[static_cast<size_t>(route)].points != nullptr;
}
double Course::routeLength(CourseRoute route) const { return path(route).length; }
MapPosition Course::locate(GeoPoint p, double corridor) const {
  return locateOn(p, corridor, CourseRoute::Regular);
}
MapPosition Course::locateOn(GeoPoint p, double corridor, CourseRoute route) const {
  MapPosition r;
  const auto route_path = path(route);
  if (!validGeo(p) || route_path.count < 2 || route_path.length <= 0) return r;
  double east = (p.longitude - data_.origin.longitude) * data_.east_per_degree;
  double north = (p.latitude - data_.origin.latitude) * data_.north_per_degree;
  const auto &m = data_.pixel_matrix;
  // Render the real coordinate, rather than silently snapping a distant fix onto the course.
  r.x = m[0] * east + m[1] * north + m[2];
  r.y = m[3] * east + m[4] * north + m[5];
  r.lateral_m = std::numeric_limits<double>::infinity();
  const size_t segments = route_path.count - 1 + (route_path.closed ? 1 : 0);
  for (size_t i = 0; i < segments; ++i) {
    const auto &a = route_path.points[i], &b = route_path.points[(i + 1) % route_path.count];
    double dx = b.east - a.east, dy = b.north - a.north, squared = dx * dx + dy * dy;
    if (squared == 0) continue;
    double t = std::clamp(((east - a.east) * dx + (north - a.north) * dy) / squared, 0.0, 1.0);
    double distance = std::hypot(east - a.east - t * dx, north - a.north - t * dy);
    if (distance < r.lateral_m) {
      r.lateral_m = distance;
      double end = i + 1 < route_path.count ? b.s : route_path.length;
      r.s_m = a.s + t * (end - a.s);
    }
  }
  r.on_course = r.lateral_m <= corridor;
  return r;
}
GeoPoint Course::pointAt(double s) const { return pointAtOn(s, CourseRoute::Regular); }
GeoPoint Course::pointAtOn(double s, CourseRoute route) const {
  const auto route_path = path(route);
  if (route_path.count < 2 || route_path.length <= 0) return data_.origin;
  if (route == CourseRoute::Regular) {
    s = std::fmod(s, route_path.length);
    if (s < 0) s += route_path.length;
  } else {
    s = std::clamp(s, 0.0, route_path.length);
  }
  const size_t segments = route_path.count - 1 + (route_path.closed ? 1 : 0);
  for (size_t i = 0; i < segments; ++i) {
    const auto &a = route_path.points[i], &b = route_path.points[(i + 1) % route_path.count];
    double end = i + 1 < route_path.count ? b.s : route_path.length;
    if (s >= a.s && s <= end) {
      if (end <= a.s) continue;
      double t = (s - a.s) / (end - a.s);
      return {data_.origin.latitude + (a.north + t * (b.north - a.north)) / data_.north_per_degree,
              data_.origin.longitude + (a.east + t * (b.east - a.east)) / data_.east_per_degree};
    }
  }
  return data_.origin;
}
double Course::forwardDelta(double from, double to) const {
  return forwardDelta(from, to, CourseRoute::Regular);
}
double Course::forwardDelta(double from, double to, CourseRoute route) const {
  double delta = to - from;
  if (route == CourseRoute::Regular) {
    const double length = routeLength(route);
    if (delta > length / 2) delta -= length;
    if (delta < -length / 2) delta += length;
  }
  return delta;
}
bool PassageDetector::update(const MapPosition &p, Millis now, double gate, double min_progress,
                             const Course &course, const Settings &s, CourseRoute route) {
  if (!p.on_course) {
    suspend();
    return false;
  }
  // Keep progress over a brief GPS outage, but never count the unseen movement.
  // A long outage starts a new passage to avoid applying an old lap to a new position.
  constexpr Millis kMaxProgressHoldMs = 30000;
  if (!initialized_ || now < previous_time_ || now - previous_time_ > s.gps_stale_ms) {
    if (has_sample_ && (now < previous_time_ || now - previous_time_ > kMaxProgressHoldMs))
      progress_ = 0;
    initialized_ = true;
    has_sample_ = true;
    previous_s_ = p.s_m;
    previous_time_ = now;
    return false;
  }
  double delta = course.forwardDelta(previous_s_, p.s_m, route);
  double to_gate = course.forwardDelta(previous_s_, gate, route);
  if (std::abs(delta) > s.max_gps_step_m) {
    suspend();
    return false;
  }
  previous_s_ = p.s_m;
  previous_time_ = now;
  progress_ = std::max(0.0, progress_ + delta);
  if (delta > 0 && to_gate > 0 && to_gate <= delta && progress_ >= min_progress) {
    progress_ = 0;
    return true;
  }
  return false;
}
}  // namespace vega
