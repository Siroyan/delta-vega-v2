#include "strategy_warning.h"

#include <cmath>

namespace vega {

void StrategyWarning::reset() {
  session_ = 0;
  announced_.fill(0);
}

bool StrategyWarning::update(uint32_t session, uint8_t lap, bool position_valid,
                             const StrategyLap &plan, double progress_m,
                             StrategyCue &cue) {
  if (!session || !lap || lap > kLapCount) {
    reset();
    return false;
  }
  if (session != session_) {
    session_ = session;
    announced_.fill(0);
  }
  if (!position_valid || !std::isfinite(progress_m) || progress_m < 0) return false;

  auto &announced = announced_[lap - 1];
  for (uint8_t i = 0; i < plan.run_count; ++i) {
    const double targets[] = {plan.runs[i].on_s_m, plan.runs[i].off_s_m};
    for (uint8_t kind = 0; kind < 2; ++kind) {
      const uint8_t bit = 1U << (2 * i + kind);
      const double remaining = targets[kind] - progress_m;
      if (!(announced & bit) && remaining > 0 && remaining <= kLeadDistanceM) {
        announced |= bit;
        cue = kind == 0 ? StrategyCue::On : StrategyCue::Off;
        return true;
      }
    }
  }
  return false;
}

}  // namespace vega
