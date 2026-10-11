#pragma once

#include <cstring>

namespace tab5 {

// Firmware before the SD catalog had no saved course selection. Its standard
// build used the Tamagawa Gakuen course, so an empty selection must probe that
// course's NVS records before falling back to the older device-wide record.
inline const char *legacyNamespaceForId(const char *id, bool older) {
  if (!id) return nullptr;
  if (!*id) id = "tamagawagakuen_station_loop_test_v1";
  struct Entry { const char *id, *current, *older; };
  static constexpr Entry migrations[] = {
      {"tamagawagakuen_station_loop_test_v1", "vega-test4", "vega-test"},
      {"tobitakyu_hospital_loop_test_v2", "vega-tobi4", "vega-tobi2"},
      {"motegi_oval_2025_full_v2", "vega", nullptr},
  };
  for (const auto &entry : migrations)
    if (!std::strcmp(id, entry.id)) return older ? entry.older : entry.current;
  return nullptr;
}

}  // namespace tab5
