#pragma once
#include <cstddef>

#include "types.h"

namespace vega {
// Bounded streaming parser; an output fix is emitted only for a checksum-valid RMC.
class NmeaParser {
 public:
  bool feed(char c, Millis received, GpsFix &fix);
  uint32_t rejected() const { return rejected_; }

 private:
  bool parse(Millis received, GpsFix &fix);
  char line_[160]{};
  size_t length_ = 0;
  bool collecting_ = false;
  uint8_t satellites_ = 0;
  uint8_t gga_fix_quality_ = 0;
  double hdop_ = 0;
  Millis quality_received_ms_ = 0;
  bool quality_valid_ = false;
  uint32_t rejected_ = 0;
};
}  // namespace vega
