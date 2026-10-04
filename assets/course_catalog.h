#pragma once

#include <cstddef>
#include <cstdint>
#include <lvgl.h>
#include "domain/course.h"
#include "domain/settings_scopes.h"

namespace tab5 {
constexpr size_t kMaxCourses = 8;
struct CourseAsset {
  char name_storage[48]{};
  char short_name_storage[24]{};
  char namespace_storage[16]{};
  char legacy_namespace_storage[16]{};
  char id_storage[48]{};
  const char *name = name_storage;
  const char *short_name = short_name_storage;
  const char *settings_namespace = namespace_storage;
  const char *legacy_settings_namespace = nullptr;
  uint32_t legacy_total_target_s = 0;
  bool pedestrian_gps = false;
  vega::CourseSettings default_settings{};
  vega::CourseData data_storage{};
  const vega::CourseData *data = &data_storage;
  lv_image_dsc_t image_storage{};
  const lv_image_dsc_t *image = &image_storage;
  vega::CoursePoint *route_points[4]{};
  uint8_t *image_pixels = nullptr;
  vega::CourseSettings defaults() const { return default_settings; }
};

// Geometry and artwork remain cached in PSRAM after card removal.
bool loadCoursesFromSd();
size_t courseCount();
const CourseAsset &courseAsset(size_t index);
size_t courseIndex(const char *id);
// NVS upgrade compatibility before microSD is mounted; not a runtime course catalog.
const char *legacyNamespaceForId(const char *id, bool older);
}  // namespace tab5
