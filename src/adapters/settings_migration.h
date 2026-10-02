#pragma once

#include "domain/settings_scopes.h"

namespace tab5 {

// A new course has no legacy blob of its own. Only its vehicle-wide settings
// may come from the old device namespace; its route and targets stay local.
template <typename ReadSelected, typename ReadDevice>
vega::GeneralSettings migratedGeneralSettings(const vega::Settings &defaults,
                                              ReadSelected read_selected,
                                              ReadDevice read_device) {
  vega::Settings source = defaults;
  if (!read_selected(source)) {
    source = defaults;
    if (!read_device(source)) source = defaults;
  }
  return vega::generalSettings(source);
}

template <typename ReadSelected>
vega::CourseSettings migratedCourseSettings(const vega::Settings &defaults,
                                            ReadSelected read_selected) {
  vega::Settings source = defaults;
  if (!read_selected(source)) source = defaults;
  return vega::courseSettings(source);
}

}  // namespace tab5
