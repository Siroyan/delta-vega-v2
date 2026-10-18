#pragma once

#include <stdint.h>

struct ControlGestureResult {
  uintptr_t pressed = 0;
  uintptr_t released = 0;
  uintptr_t tapped = 0;
};

// A tap must start and finish on the same enabled control. A hold ends on
// physical release even if the control becomes disabled while it is held.
class ControlGesture {
 public:
  ControlGestureResult sample(bool pressed, uintptr_t hit) {
    ControlGestureResult result;
    if (pressed) {
      if (!touching_) {
        touching_ = true;
        held_ = tap_target_ = hit;
        result.pressed = hit;
      } else if (hit != tap_target_) {
        tap_target_ = 0;
      }
      return result;
    }
    if (!touching_) return result;
    touching_ = false;
    result.released = held_;
    result.tapped = tap_target_ && tap_target_ == hit ? tap_target_ : 0;
    held_ = tap_target_ = 0;
    return result;
  }

  bool touching() const { return touching_; }

 private:
  bool touching_ = false;
  uintptr_t held_ = 0, tap_target_ = 0;
};

enum class PowerRequest : uint8_t { None, On, Off };

// Keeps a fast second tap safe while the application has not yet reported the
// first command. OFF can always follow ON; ON waits for OFF acknowledgement.
class PowerIntent {
 public:
  void observe(bool on) {
    observed_on_ = on;
    if (pending_ && observed_on_ == desired_on_) pending_ = false;
  }

  PowerRequest nextTap() const {
    if (pending_ && !desired_on_) return PowerRequest::None;
    return (pending_ ? desired_on_ : observed_on_) ? PowerRequest::Off : PowerRequest::On;
  }

  void accepted(PowerRequest command) {
    if (command == PowerRequest::None) return;
    desired_on_ = command == PowerRequest::On;
    pending_ = true;
  }

 private:
  bool observed_on_ = false;
  bool desired_on_ = false;
  bool pending_ = false;
};
