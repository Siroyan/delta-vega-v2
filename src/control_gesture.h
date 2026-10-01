#pragma once

#include <stdint.h>

// A touch must start and finish on the same enabled control. Once the finger
// leaves it, returning to the control does not turn that drag into a tap.
class ControlGesture {
 public:
  uintptr_t sample(bool pressed, uintptr_t hit) {
    if (pressed) {
      if (!touching_) {
        touching_ = true;
        target_ = hit;
      } else if (hit != target_) {
        target_ = 0;
      }
      return 0;
    }
    if (!touching_) return 0;
    touching_ = false;
    const uintptr_t accepted = target_ && target_ == hit ? target_ : 0;
    target_ = 0;
    return accepted;
  }

  bool touching() const { return touching_; }

 private:
  bool touching_ = false;
  uintptr_t target_ = 0;
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
