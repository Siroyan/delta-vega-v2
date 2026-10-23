#pragma once

#include "strategy_motion.h"

namespace sim {
// Filesystem/serial adapter. The JSON bytes are identical to the Tab5 file.
class StrategyStorage {
 public:
  bool load(const char *filename, const CourseSpec &course, std::string &error);
  void clear();
  bool beginUpload(size_t bytes, const char *sha256, std::string &error);
  bool chunk(const char *hex, std::string &error);
  bool finishUpload(const CourseSpec &course, std::string &error);
  size_t received() const { return received_; }
  const DrivingStrategy *plan() const { return active_ ? &plan_ : nullptr; }
  const std::string &filename() const { return filename_; }
  const std::string &sha256() const { return sha256_; }

 private:
  bool accept(const std::string &bytes, const CourseSpec &course,
              const char *name, std::string &error, bool persist);
  DrivingStrategy plan_;
  bool active_ = false;
  std::string filename_, sha256_, upload_, expected_sha256_;
  size_t received_ = 0;
};
}  // namespace sim
