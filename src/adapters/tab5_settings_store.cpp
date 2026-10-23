#include "tab5_settings_store.h"

#include <Preferences.h>
#include <nvs.h>

#include <cstdio>
#include <cstring>

#include "course_catalog.h"
#include "domain/settings_codec.h"
#include "settings_migration.h"

namespace tab5 {

SavedSelection loadSelection() {
  SavedSelection value{};
  Preferences p;
  if (p.begin("vega-select", true)) {
    if (p.getBytesLength("choice") == sizeof(value)) {
      SavedSelection stored{};
      if (p.getBytes("choice", &stored, sizeof(stored)) == sizeof(stored) &&
          stored.version == 1 && memchr(stored.course_id, 0, sizeof(stored.course_id)) &&
          memchr(stored.plan, 0, sizeof(stored.plan))) value = stored;
    }
    p.end();
  }
  return value;
}

uint8_t savedCourseIndex() {
  const auto selection = loadSelection();
  return static_cast<uint8_t>(courseIndex(selection.course_id));
}

void savedPlanName(char *out, size_t size) {
  const auto selection = loadSelection();
  snprintf(out, size, "%s", selection.plan);
}

bool saveSelection(const char *course_id, const char *plan) {
  SavedSelection value{};
  if (strlen(course_id) >= sizeof(value.course_id) || strlen(plan) >= sizeof(value.plan))
    return false;
  snprintf(value.course_id, sizeof(value.course_id), "%s", course_id);
  snprintf(value.plan, sizeof(value.plan), "%s", plan);
  Preferences p;
  if (!p.begin("vega-select", false)) return false;
  const bool ok = p.putBytes("choice", &value, sizeof(value)) == sizeof(value);
  p.end();
  return ok;
}

void SettingsStore::report(const char *message) const {
  if (reporter_) reporter_(message);
}

// 車両共通設定とコース設定を別々に読み、旧版の一体型 NVS を必要時だけ移行する。
// 電装出力の OFF 極性が必要なため、SD マウント前には保存済みのコース ID から
// 旧版の NVS 名を引く。選択記録がない旧標準ビルドもここで扱う。
vega::Settings SettingsStore::load(uint8_t index) {
  const auto &asset = courseAsset(index);
  const vega::Settings base = defaults(index);
  const auto read_selected = [index](vega::Settings &s) {
    if (courseCount()) return readLegacy(index, s);
    const SavedSelection selected = loadSelection();
    const char *current = legacyNamespaceForId(selected.course_id, false);
    const char *older = legacyNamespaceForId(selected.course_id, true);
    return (current && loadFrom(current, s)) || (older && loadFrom(older, s));
  };

  if (!general_loaded_) {
    vega::GeneralSettings stored;
    bool needs_upgrade = false;
    if (!readGeneral(stored, needs_upgrade)) {
      stored = migratedGeneralSettings(base, read_selected,
                                       [](vega::Settings &s) { return loadFrom("vega", s); });
      if (saveGeneral(stored)) report("[SETTINGS] migrated general settings");
    } else if (needs_upgrade && saveGeneral(stored)) {
      report("[SETTINGS] upgraded general settings to v2");
    }
    general_ = stored;
    general_loaded_ = true;
  }

  vega::CourseSettings course;
  if (!readCourse(index, course)) {
    course = migratedCourseSettings(base, read_selected);
    if (writeCourse(index, course)) {
      char message[100];
      snprintf(message, sizeof(message), "[SETTINGS] migrated course=%s", asset.data->id);
      report(message);
    }
  }

  vega::Settings settings = base;
  vega::applyGeneral(settings, general_);
  vega::applyCourse(settings, course);
  return settings;
}

bool SettingsStore::saveGeneral(const vega::GeneralSettings &settings) {
  if (!vega::validGeneralSettings(settings)) return false;
  Preferences p;
  if (!p.begin("vega-device", false)) return false;
  const bool ok = p.putBytes("settings", &settings, sizeof(settings)) == sizeof(settings);
  p.end();
  if (ok) general_ = settings;
  return ok;
}

bool SettingsStore::saveCourse(const vega::CourseSettings &settings) {
  return writeCourse(active_, settings);
}

vega::Settings SettingsStore::defaults(uint8_t index) {
  vega::Settings settings;
  vega::applyCourse(settings, courseAsset(index).defaults());
  return settings;
}

bool SettingsStore::readGeneral(vega::GeneralSettings &settings, bool &needs_upgrade) {
  Preferences p;
  if (!p.begin("vega-device", true)) return false;
  const size_t size = p.getBytesLength("settings");
  alignas(vega::GeneralSettings) uint8_t blob[sizeof(vega::GeneralSettings)]{};
  vega::GeneralSettings candidate;
  const bool ok = size > 0 && size <= sizeof(blob) &&
                  p.getBytes("settings", blob, size) == size &&
                  vega::decodeGeneralSettingsBlob(blob, size, candidate);
  p.end();
  if (ok) {
    settings = candidate;
    needs_upgrade = size != sizeof(vega::GeneralSettings);
  }
  return ok;
}

bool SettingsStore::readCourse(uint8_t index, vega::CourseSettings &settings) {
  Preferences p;
  if (!p.begin(courseAsset(index).settings_namespace, true)) return false;
  vega::CourseSettings candidate;
  const bool ok = p.getBytesLength("course") == sizeof(candidate) &&
                  p.getBytes("course", &candidate, sizeof(candidate)) == sizeof(candidate) &&
                  vega::validCourseSettings(candidate);
  p.end();
  if (ok) settings = candidate;
  return ok;
}

bool SettingsStore::writeCourse(uint8_t index, const vega::CourseSettings &settings) {
  if (!vega::validCourseSettings(settings)) return false;
  Preferences p;
  if (!p.begin(courseAsset(index).settings_namespace, false)) return false;
  const bool ok = p.putBytes("course", &settings, sizeof(settings)) == sizeof(settings);
  p.end();
  return ok;
}

bool SettingsStore::readLegacy(uint8_t index, vega::Settings &settings) {
  const auto &asset = courseAsset(index);
  if (loadFrom(asset.settings_namespace, settings)) return true;
  if (!asset.legacy_settings_namespace || !loadFrom(asset.legacy_settings_namespace, settings))
    return false;
  if (asset.legacy_total_target_s && settings.total_target_s == asset.legacy_total_target_s)
    settings.total_target_s = asset.defaults().total_target_s;
  return true;
}

bool SettingsStore::loadFrom(const char *name, vega::Settings &settings) {
  nvs_handle_t handle;
  bool loaded = false;
  if (nvs_open(name, NVS_READONLY, &handle) == ESP_OK) {
    size_t size = 0;
    if (nvs_get_blob(handle, "settings", nullptr, &size) == ESP_OK &&
        size <= sizeof(vega::Settings)) {
      alignas(vega::Settings) uint8_t blob[sizeof(vega::Settings)]{};
      if (nvs_get_blob(handle, "settings", blob, &size) == ESP_OK)
        loaded = vega::decodeSettingsBlob(blob, size, settings);
    }
    nvs_close(handle);
  }
  return loaded;
}

}  // namespace tab5
