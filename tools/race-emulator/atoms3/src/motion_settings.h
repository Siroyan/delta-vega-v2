#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "strategy_motion.h"

namespace sim {

enum class MotionSettingsLoad { Saved, Legacy, Invalid };

namespace motion_settings_detail {
constexpr const char *kKey = "motion";
struct Stored {
  uint32_t version = 1;
  uint32_t reserved = 0;
  double max_speed_kmh;
  double initial_speed_kmh;
  double acceleration_mps2;
  double coast_deceleration_mps2;
};
static_assert(sizeof(Stored) == 40, "NVS motion record layout changed");

// Only repair the exact float representation of a valid boundary. Do not
// widen validation to admit genuinely out-of-range or non-finite values.
inline double legacyBoundary(double value, double minimum, double maximum) {
  if (value < minimum && value == static_cast<double>(static_cast<float>(minimum))) return minimum;
  if (value > maximum && value == static_cast<double>(static_cast<float>(maximum))) return maximum;
  return value;
}
}  // namespace motion_settings_detail

// Storage uses the Preferences byte/float API; the native tests use an in-memory
// store with the same operations. One NVS record keeps related values together
// and retains the double precision used by MotionConfig.
template <class Storage>
bool saveMotionSettings(Storage &storage, const MotionConfig &config) {
  if (!validMotionConfig(config)) return false;
  const motion_settings_detail::Stored saved{
      1, 0, config.max_speed_kmh, config.initial_speed_kmh,
      config.acceleration_mps2, config.coast_deceleration_mps2};
  return storage.putBytes(motion_settings_detail::kKey, &saved, sizeof(saved)) == sizeof(saved);
}

// Failure leaves the caller's defaults intact. Legacy settings are returned
// separately so the device can migrate once and report a failed NVS write.
template <class Storage>
MotionSettingsLoad loadMotionSettings(Storage &storage, MotionConfig &config) {
  using namespace motion_settings_detail;
  MotionConfig next;
  if (storage.isKey(kKey)) {
    Stored saved{};
    if (storage.getBytesLength(kKey) != sizeof(saved) ||
        storage.getBytes(kKey, &saved, sizeof(saved)) != sizeof(saved) || saved.version != 1)
      return MotionSettingsLoad::Invalid;
    next = {saved.max_speed_kmh, saved.initial_speed_kmh,
            saved.acceleration_mps2, saved.coast_deceleration_mps2};
    if (!validMotionConfig(next)) return MotionSettingsLoad::Invalid;
    config = next;
    return MotionSettingsLoad::Saved;
  }

  const auto legacy = [&](const char *key, double fallback) -> double {
    return storage.isKey(key) ? storage.getFloat(key) : fallback;
  };
  next = {legacy("speed", config.max_speed_kmh), legacy("initial", config.initial_speed_kmh),
          legacy("accel", config.acceleration_mps2), legacy("coast", config.coast_deceleration_mps2)};
  next.max_speed_kmh = legacyBoundary(next.max_speed_kmh, 1, 60);
  next.acceleration_mps2 = legacyBoundary(next.acceleration_mps2, 0.01, 5);
  next.coast_deceleration_mps2 = legacyBoundary(next.coast_deceleration_mps2, 0.001, 2);
  // Older firmware capped initial speed in RAM but did not persist the cap.
  // Repair that combination without accepting invalid legacy initial speeds.
  if (std::isfinite(next.initial_speed_kmh) && next.initial_speed_kmh >= 0 && next.initial_speed_kmh <= 60)
    next.initial_speed_kmh = std::min(next.initial_speed_kmh, next.max_speed_kmh);
  if (!validMotionConfig(next)) return MotionSettingsLoad::Invalid;
  config = next;
  return MotionSettingsLoad::Legacy;
}

}  // namespace sim
