#pragma once
#include "domain/types.h"
namespace vega {
constexpr size_t kSettingsFieldCount = 14;
// Fields: total, lap 1..7, start lat/lon, timing lat/lon, goal lat/lon.
bool editSetting(Settings &settings, size_t field, const char *text);
void settingText(const Settings &settings, size_t field, char *out, size_t capacity);
const char *settingTitle(size_t field);
}  // namespace vega
