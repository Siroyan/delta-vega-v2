#pragma once
#include "types.h"

namespace vega {
struct CoursePoint {
  double east, north, s;
};
struct CourseData {
  const CoursePoint *points;
  size_t count;
  double length;
  GeoPoint origin;
  double east_per_degree, north_per_degree;
  std::array<double, 6> pixel_matrix;
};
class Course {
 public:
  explicit Course(const CourseData &data) : data_(data) {}
  MapPosition locate(GeoPoint p, double corridor) const;
  GeoPoint pointAt(double s) const;
  double length() const { return data_.length; }
  double forwardDelta(double from, double to) const;

 private:
  CourseData data_;
};
class PassageDetector {
 public:
  void reset() {
    initialized_ = false;
    progress_ = 0;
  }
  bool update(const MapPosition &position, Millis now, double gate_s, double min_progress,
              const Course &course, const Settings &settings);

 private:
  bool initialized_ = false;
  double previous_s_ = 0, progress_ = 0;
  Millis previous_time_ = 0;
};
}  // namespace vega
