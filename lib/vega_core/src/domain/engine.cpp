#include "engine.h"
namespace vega {
void EngineCommands::power(bool on, Millis now, const Settings &s) {
  if (!on) {
    phase_ = EnginePhase::Off;
    deadline_ = 0;
  } else if (phase_ == EnginePhase::Off) {
    phase_ = EnginePhase::Preparing;
    deadline_ = now + s.ecu_ready_ms;
  }
}
bool EngineCommands::ignite(Millis now, const Settings &s) {
  tick(now);
  if (phase_ != EnginePhase::Ready) return false;
  phase_ = EnginePhase::Pulsing;
  deadline_ = now + s.ignition_pulse_ms;
  return true;
}
void EngineCommands::tick(Millis now) {
  if (now < deadline_) return;
  if (phase_ == EnginePhase::Preparing)
    phase_ = EnginePhase::Ready;
  else if (phase_ == EnginePhase::Pulsing)
    phase_ = EnginePhase::Issued;
}
uint16_t EngineCommands::preparationPermille(Millis now, uint32_t duration_ms) const {
  if (phase_ != EnginePhase::Preparing) return 0;
  if (duration_ms == 0 || now >= deadline_) return 1000;
  const Millis remaining = deadline_ - now;
  if (remaining >= duration_ms) return 0;
  return static_cast<uint16_t>((duration_ms - remaining) * 1000 / duration_ms);
}
}  // namespace vega
