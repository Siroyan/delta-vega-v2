#include "motion_settings.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace sim;

// Emulate NVS across boots by retaining the same byte store while discarding
// all MotionConfig instances. Float writes really round to 32 bits.
struct Storage {
  std::map<std::string, std::vector<unsigned char>> records;
  bool fail_write = false, fail_read = false;
  bool isKey(const char *key) const { return records.count(key); }
  size_t putBytes(const char *key, const void *data, size_t size) {
    if (fail_write) return 0;
    const auto *bytes = static_cast<const unsigned char *>(data);
    records[key] = {bytes, bytes + size};
    return size;
  }
  size_t getBytesLength(const char *key) const { return records.at(key).size(); }
  size_t getBytes(const char *key, void *data, size_t size) const {
    const auto &bytes = records.at(key);
    if (fail_read || bytes.size() > size) return 0;
    std::memcpy(data, bytes.data(), bytes.size());
    return bytes.size();
  }
  void putFloat(const char *key, float value) { putBytes(key, &value, sizeof(value)); }
  float getFloat(const char *key) const {
    float value = 0;
    assert(getBytes(key, &value, sizeof(value)) == sizeof(value));
    return value;
  }
};

static void equal(const MotionConfig &a, const MotionConfig &b) {
  assert(a.max_speed_kmh == b.max_speed_kmh);
  assert(a.initial_speed_kmh == b.initial_speed_kmh);
  assert(a.acceleration_mps2 == b.acceleration_mps2);
  assert(a.coast_deceleration_mps2 == b.coast_deceleration_mps2);
}

int main() {
  // All accepted boundaries and non-integer settings round-trip without loss.
  for (const auto &config : {MotionConfig{1, 1, 0.01, 0.001}, MotionConfig{60, 60, 5, 2},
                             MotionConfig{30.123456789, 9.87654321, 0.123456789, 0.0123456789}}) {
    Storage storage;
    assert(saveMotionSettings(storage, config));
    MotionConfig rebooted;
    assert(loadMotionSettings(storage, rebooted) == MotionSettingsLoad::Saved);
    equal(rebooted, config);
  }

  Storage empty;
  MotionConfig defaults{12, 0, 0.3, 0.03};
  MotionConfig initial = defaults;
  assert(loadMotionSettings(empty, initial) == MotionSettingsLoad::Legacy);
  equal(initial, defaults);

  // Persist the complete post-clamp configuration when reducing max speed.
  Storage capped;
  MotionConfig before{30, 10, 0.4, 0.2};
  assert(saveMotionSettings(capped, before));
  MotionConfig after = before;
  after.max_speed_kmh = 5;
  after.initial_speed_kmh = std::min(after.initial_speed_kmh, after.max_speed_kmh);
  assert(saveMotionSettings(capped, after));
  MotionConfig rebooted;
  assert(loadMotionSettings(capped, rebooted) == MotionSettingsLoad::Saved);
  equal(rebooted, after);

  // Migrate the exact legacy float boundary without discarding other fields.
  Storage legacy;
  legacy.putFloat("speed", 30);
  legacy.putFloat("initial", 10);
  legacy.putFloat("accel", 0.01);
  legacy.putFloat("coast", 0.2);
  assert(legacy.getFloat("accel") < 0.01);  // reproduce the old rejection
  MotionConfig migrated;
  assert(loadMotionSettings(legacy, migrated) == MotionSettingsLoad::Legacy);
  assert(migrated.acceleration_mps2 == 0.01);
  assert(migrated.initial_speed_kmh == 10);
  assert(migrated.coast_deceleration_mps2 == legacy.getFloat("coast"));
  assert(saveMotionSettings(legacy, migrated));
  assert(loadMotionSettings(legacy, rebooted) == MotionSettingsLoad::Saved);
  equal(rebooted, migrated);

  // Repair legacy speed/initial pairs left by the former RAM-only clamp.
  Storage legacy_cap;
  legacy_cap.putFloat("speed", 5);
  legacy_cap.putFloat("initial", 10);
  legacy_cap.putFloat("accel", 0.4);
  legacy_cap.putFloat("coast", 0.2);
  assert(loadMotionSettings(legacy_cap, migrated) == MotionSettingsLoad::Legacy);
  assert(migrated.max_speed_kmh == 5 && migrated.initial_speed_kmh == 5);
  assert(migrated.acceleration_mps2 == legacy_cap.getFloat("accel"));
  assert(migrated.coast_deceleration_mps2 == legacy_cap.getFloat("coast"));
  assert(saveMotionSettings(legacy_cap, migrated));
  // A new record takes precedence over the leftover legacy keys.
  legacy_cap.putFloat("accel", 0);
  assert(loadMotionSettings(legacy_cap, rebooted) == MotionSettingsLoad::Saved);
  equal(rebooted, migrated);

  // Out-of-range values and non-finite values still fail, preserving defaults.
  for (double bad : {0.009, 6.0, std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::infinity()}) {
    Storage invalid;
    invalid.putFloat("accel", bad);
    rebooted = defaults;
    assert(loadMotionSettings(invalid, rebooted) == MotionSettingsLoad::Invalid);
    equal(rebooted, defaults);
    MotionConfig invalid_config = before;
    invalid_config.acceleration_mps2 = bad;
    assert(!saveMotionSettings(invalid, invalid_config));
  }
  Storage bad_initial;
  bad_initial.putFloat("initial", 61);
  rebooted = defaults;
  assert(loadMotionSettings(bad_initial, rebooted) == MotionSettingsLoad::Invalid);
  equal(rebooted, defaults);

  // Reject damaged/newer records rather than falling back to stale legacy data.
  Storage corrupt;
  assert(saveMotionSettings(corrupt, before));
  corrupt.records["motion"][0] = 2;
  rebooted = defaults;
  assert(loadMotionSettings(corrupt, rebooted) == MotionSettingsLoad::Invalid);
  equal(rebooted, defaults);
  corrupt.records["motion"].resize(8);
  assert(loadMotionSettings(corrupt, rebooted) == MotionSettingsLoad::Invalid);
  equal(rebooted, defaults);

  Storage failed;
  assert(saveMotionSettings(failed, before));
  failed.fail_write = true;
  assert(!saveMotionSettings(failed, after));
  assert(loadMotionSettings(failed, rebooted) == MotionSettingsLoad::Saved);
  equal(rebooted, before);
  failed.fail_read = true;
  rebooted = defaults;
  assert(loadMotionSettings(failed, rebooted) == MotionSettingsLoad::Invalid);
  equal(rebooted, defaults);
  std::cout << "PASS: motion settings boundaries, restart, legacy migration, speed cap, invalid records\n";
}
