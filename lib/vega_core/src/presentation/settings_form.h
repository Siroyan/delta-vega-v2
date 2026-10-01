#pragma once
#include "domain/types.h"
namespace vega {
constexpr size_t kSettingsFieldCount = 27;
// Fields: total, lap 1..7, start/timing/goal lat/lon, then vehicle and
// measurement parameters. Keep the first 14 indices stable for the EEZ UI.
bool editSetting(Settings &settings, size_t field, const char *text);
void settingText(const Settings &settings, size_t field, char *out, size_t capacity);
const char *settingTitle(size_t field);
}  // namespace vega
