#pragma once
#include "types.h"

namespace vega {
class EngineCommands {
 public:
  void power(bool on, Millis now, const Settings &s);
  bool ignite(Millis now, const Settings &s);
  void tick(Millis now);
  EnginePhase phase() const { return phase_; }
  uint16_t preparationPermille(Millis now, uint32_t duration_ms) const;

 private:
  EnginePhase phase_ = EnginePhase::Off;
  Millis deadline_ = 0;
};
}  // namespace vega
