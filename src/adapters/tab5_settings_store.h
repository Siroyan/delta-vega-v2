#pragma once

#include <cstddef>
#include <cstdint>

#include "ports/ports.h"

namespace tab5 {

struct SavedSelection {
  uint32_t version = 1;
  char course_id[48]{};
  char plan[48]{};
};

SavedSelection loadSelection();
uint8_t savedCourseIndex();
void savedPlanName(char *out, size_t size);
bool saveSelection(const char *course_id, const char *plan);

// NVS への保存と旧版からの移行を担当する。起動時には SD のマウント前にも呼ばれる。
class SettingsStore final : public vega::ISettingsStore {
 public:
  using Reporter = void (*)(const char *message);
  explicit SettingsStore(Reporter reporter) : reporter_(reporter) {}

  void select(uint8_t index) { active_ = index; }
  vega::Settings load(uint8_t index);
  bool saveGeneral(const vega::GeneralSettings &settings) override;
  bool saveCourse(const vega::CourseSettings &settings) override;

 private:
  uint8_t active_ = 0;
  bool general_loaded_ = false;
  vega::GeneralSettings general_{};
  Reporter reporter_;

  void report(const char *message) const;
  static vega::Settings defaults(uint8_t index);
  static bool readGeneral(vega::GeneralSettings &settings, bool &needs_upgrade);
  static bool readCourse(uint8_t index, vega::CourseSettings &settings);
  static bool writeCourse(uint8_t index, const vega::CourseSettings &settings);
  static bool readLegacy(uint8_t index, vega::Settings &settings);
  static bool loadFrom(const char *name, vega::Settings &settings);
};

}  // namespace tab5
