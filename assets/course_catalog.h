#pragma once

#include <cstddef>
#include <cstdint>
#include <lvgl.h>
#include "domain/course.h"
#include "domain/settings_scopes.h"

namespace tab5 {
struct CourseAsset {
  const char *name;
  const char *short_name;
  const vega::CourseData *data;
  const lv_image_dsc_t *image;
  const char *settings_namespace;
  const char *legacy_settings_namespace;
  uint32_t legacy_total_target_s;
  vega::CourseSettings (*defaults)();
  bool pedestrian_gps;
};

size_t courseCount();
const CourseAsset &courseAsset(size_t index);
size_t courseIndex(const char *id);
}  // namespace tab5
