#pragma once

#include <cstdint>

namespace tab5 {

inline uint32_t wheelIntervalUs(double circumference_m, uint32_t pulses_per_revolution,
                                double guard_speed_kmh, uint32_t configured_debounce_us) {
  const auto speed_limit_us = static_cast<uint32_t>(
      circumference_m * 3600000.0 / (pulses_per_revolution * guard_speed_kmh));
  return configured_debounce_us > speed_limit_us ? configured_debounce_us : speed_limit_us;
}

// A reed contact can open and close several times during one pass. Count the
// first closure, then require a continuously open interval before rearming.
// All calls, including reset(), must be serialized by the GPIO owner's lock.
class ReedPulseFilter {
 public:
  void reset(bool high, uint64_t now_us) {
    high_ = high;
    high_since_us_ = now_us;
    last_accepted_us_ = 0;
    accepted_ = 0;
    raw_falls_ = 0;
    rejected_release_ = 0;
    rejected_interval_ = 0;
  }

  __attribute__((always_inline)) inline bool edge(bool high, uint64_t now_us,
                                                   uint32_t release_us, uint32_t interval_us) {
    if (high == high_) return false;
    high_ = high;
    if (high) {
      high_since_us_ = now_us;
      return false;
    }

    ++raw_falls_;
    if (now_us < high_since_us_ || now_us - high_since_us_ < release_us) {
      ++rejected_release_;
      return false;
    }
    if (accepted_ && (now_us < last_accepted_us_ ||
                      now_us - last_accepted_us_ < interval_us)) {
      ++rejected_interval_;
      return false;
    }
    last_accepted_us_ = now_us;
    ++accepted_;
    return true;
  }

  uint64_t rawFalls() const { return raw_falls_; }
  uint64_t rejectedRelease() const { return rejected_release_; }
  uint64_t rejectedInterval() const { return rejected_interval_; }

 private:
  bool high_ = true;
  uint64_t high_since_us_ = 0;
  uint64_t last_accepted_us_ = 0;
  uint64_t accepted_ = 0;
  uint64_t raw_falls_ = 0;
  uint64_t rejected_release_ = 0;
  uint64_t rejected_interval_ = 0;
};

}  // namespace tab5
