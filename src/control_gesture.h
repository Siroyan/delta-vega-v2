#pragma once

#include <stdint.h>

struct ControlTouch {
  // The physical press timestamp identifies a gesture across both tasks.
  uint64_t gesture_us = 0;
  uint64_t sampled_us = 0;
};

// Access under the touch sampler's lock. Keep enough releases for a full input
// queue even if the UI's separate action queue overflows. Expired presses are
// rejected. Prepare before queuing IGNITE, then activate after it is accepted:
// a physical release between those steps is retained, not delivered too early.
class ControlReleaseLatch {
 public:
  static constexpr unsigned kHistorySize = 32;

  void observePress(uint64_t gesture_us) { latest_gesture_us_ = gesture_us; }

  bool prepare(uint64_t gesture_us) {
    if (!gesture_us || prepared_gesture_us_) return false;
    uint64_t released_us = 0;
    for (const auto &release : releases_)
      if (release.gesture_us == gesture_us) released_us = release.sampled_us;
    if (!released_us && gesture_us != latest_gesture_us_) return false;
    prepared_gesture_us_ = gesture_us;
    prepared_release_us_ = released_us;
    return true;
  }

  uint64_t activate() {
    if (!prepared_gesture_us_) return 0;
    active_ = true;
    return finish(prepared_gesture_us_, prepared_release_us_);
  }

  void cancel() {
    prepared_gesture_us_ = prepared_release_us_ = 0;
    active_ = false;
  }

  uint64_t observeRelease(const ControlTouch &touch) {
    releases_[next_release_] = touch;
    next_release_ = (next_release_ + 1) % kHistorySize;
    return finish(touch.gesture_us, touch.sampled_us);
  }

  uint64_t finish(uint64_t gesture_us, uint64_t released_us) {
    if (!prepared_gesture_us_ || gesture_us != prepared_gesture_us_ || !released_us) return 0;
    if (!prepared_release_us_) prepared_release_us_ = released_us;
    if (!active_) return 0;
    const auto completed_us = prepared_release_us_;
    cancel();
    return completed_us;
  }

 private:
  uint64_t latest_gesture_us_ = 0, prepared_gesture_us_ = 0, prepared_release_us_ = 0;
  ControlTouch releases_[kHistorySize]{};
  unsigned next_release_ = 0;
  bool active_ = false;
};

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
