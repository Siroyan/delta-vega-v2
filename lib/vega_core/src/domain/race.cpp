#include "race.h"

namespace vega {
bool RaceSession::start(Millis now, uint64_t pulses) {
  if (phase_ != RacePhase::Waiting) return false;
  phase_ = RacePhase::Measuring;
  start_ = lap_start_ = last_update_ = now;
  start_pulses_ = pulses;
  lap_ = 1;
  approximate_ = false;
  ++session_;
  return true;
}
bool RaceSession::cancel() {
  if (phase_ != RacePhase::Measuring) return false;
  phase_ = RacePhase::Waiting;
  lap_ = 0;
  approximate_ = false;
  return true;
}
bool RaceSession::advance(Millis now, bool manual, const Settings &s) {
  if (phase_ != RacePhase::Measuring || lap_ >= lap_count_ || now < last_update_ ||
      now - last_update_ < s.lap_duplicate_ms || (!manual && sinceLap(now) < s.min_lap_ms))
    return false;
  ++lap_;
  lap_start_ = last_update_ = now;
  approximate_ = manual;
  return true;
}
bool RaceSession::finish(Millis now, uint64_t pulses, const Settings &s) {
  if (phase_ != RacePhase::Measuring || lap_ != lap_count_ || now < last_update_)
    return false;
  result_ = reading(now, pulses, s);
  phase_ = result_.phase = RacePhase::Finished;
  return true;
}
RaceReading RaceSession::reading(Millis now, uint64_t pulses, const Settings &s) const {
  if (phase_ == RacePhase::Finished) return result_;
  RaceReading r;
  r.phase = phase_;
  r.lap = lap_;
  r.session = session_;
  r.lap_approximate = approximate_;
  if (phase_ != RacePhase::Measuring) return r;
  r.total_ms = now >= start_ ? now - start_ : 0;
  r.lap_ms = sinceLap(now);
  r.distance_m = (pulses >= start_pulses_ ? pulses - start_pulses_ : 0) * s.wheel_circumference_m /
                 s.pulses_per_revolution;
  r.average_kmh = r.total_ms ? r.distance_m * 3600.0 / r.total_ms : 0;
  return r;
}
WheelReading wheelReading(WheelInput w, uint64_t now, const Settings &s) {
  WheelReading r;
  r.pulses = w.pulses;
  r.valid = w.pulses > 0 && w.last_pulse_us <= now;
  if (!r.valid) return r;
  auto age = now - w.last_pulse_us;
  r.pulse_recent = age <= 200000;
  // A stationary wheel and a disconnected reed cannot be distinguished after a first pulse.
  if (age >= uint64_t(s.speed_zero_ms) * 1000) return r;
  if (w.previous_pulse_us == 0 || w.last_pulse_us <= w.previous_pulse_us) {
    r.valid = false;
    return r;
  }
  auto period = w.last_pulse_us - w.previous_pulse_us;
  if (age > period) period = age;
  r.speed_kmh = s.wheel_circumference_m * 3600000.0 / (s.pulses_per_revolution * period);
  return r;
}
}  // namespace vega
