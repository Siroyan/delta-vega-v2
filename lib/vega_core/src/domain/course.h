#pragma once
#include "types.h"

namespace vega {
struct CoursePoint {
  double east, north, s;
};
enum class CourseRoute : uint8_t { First, Regular, Final, FinishApproach };
struct CoursePath {
  const CoursePoint *points = nullptr;
  size_t count = 0;
  double length = 0;
  bool closed = false;
};
struct CourseData {
  const CoursePoint *points;
  size_t count;
  double length;
  GeoPoint origin;
  double east_per_degree, north_per_degree;
  std::array<double, 6> pixel_matrix;
  // Schema 2 routes. Empty paths fall back to the legacy closed course.
  std::array<CoursePath, 4> routes{};
  const char *id = nullptr;
  uint8_t lap_count = kLapCount;
};
class Course {
 public:
  explicit Course(const CourseData &data) : data_(data) {}
  MapPosition locate(GeoPoint p, double corridor) const;
  MapPosition locateOn(GeoPoint p, double corridor, CourseRoute route) const;
  GeoPoint pointAt(double s) const;
  GeoPoint pointAtOn(double s, CourseRoute route) const;
  double length() const { return routeLength(CourseRoute::Regular); }
  double routeLength(CourseRoute route) const;
  bool hasRoute(CourseRoute route) const;
  uint8_t lapCount() const {
    return data_.lap_count >= 2 && data_.lap_count <= kMaxRaceLaps ? data_.lap_count : kLapCount;
  }
  double forwardDelta(double from, double to) const;
  double forwardDelta(double from, double to, CourseRoute route) const;

 private:
  CoursePath path(CourseRoute route) const;
  CourseData data_;
};
class PassageDetector {
 public:
  void reset() {
    initialized_ = false;
    has_sample_ = false;
    progress_ = 0;
    previous_time_ = 0;
  }
  void suspend() { initialized_ = false; }
  double progress() const { return progress_; }
  bool update(const MapPosition &position, Millis now, double gate_s, double min_progress,
              const Course &course, const Settings &settings,
              CourseRoute route = CourseRoute::Regular);

 private:
  bool initialized_ = false;
  bool has_sample_ = false;
  double previous_s_ = 0, progress_ = 0;
  Millis previous_time_ = 0;
};
}  // namespace vega
