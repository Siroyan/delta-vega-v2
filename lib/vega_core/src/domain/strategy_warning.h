#pragma once

#include <array>
#include <cstdint>

#include "strategy.h"

namespace vega {

// One advance warning per ON/OFF point, per lap and timing session.
// GPS loss does not re-arm an already announced point.
class StrategyWarning {
 public:
  static constexpr double kLeadDistanceM = 30.0;

  bool update(uint32_t session, uint8_t lap, bool position_valid,
              const StrategyLap &plan, double progress_m, StrategyCue &cue);
  void reset();

 private:
  uint32_t session_ = 0;
  std::array<uint8_t, kLapCount> announced_{};
};

}  // namespace vega
