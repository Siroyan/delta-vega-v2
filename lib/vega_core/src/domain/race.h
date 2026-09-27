#pragma once
#include "types.h"

namespace vega {
class RaceSession {
 public:
  bool start(Millis now, uint64_t pulses);
  bool cancel();
  bool advance(Millis now, bool manual, const Settings &s);
  bool finish(Millis now, uint64_t pulses, const Settings &s);
  RaceReading reading(Millis now, uint64_t pulses, const Settings &s) const;
  RacePhase phase() const { return phase_; }
  uint8_t lap() const { return lap_; }
  Millis sinceLap(Millis now) const { return now >= lap_start_ ? now - lap_start_ : 0; }

 private:
  RacePhase phase_ = RacePhase::Waiting;
  Millis start_ = 0, lap_start_ = 0, last_update_ = 0;
  uint64_t start_pulses_ = 0;
  uint32_t session_ = 0;
  uint8_t lap_ = 0;
  bool approximate_ = false;
  RaceReading result_{};
};
WheelReading wheelReading(WheelInput input, uint64_t now_us, const Settings &settings);
}  // namespace vega
