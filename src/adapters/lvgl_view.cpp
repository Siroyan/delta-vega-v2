#include "lvgl_view.h"

#include <Arduino.h>
#include <lvgl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../ui/actions.h"
#include "../ui/fonts.h"
#include "../ui/images.h"
#include "../ui/screens.h"
#include "../ui/ui.h"
#include "../tab5_lvgl.h"
#include "../control_gesture.h"
#include "course_catalog.h"
#include "domain/course.h"
#include "presentation/settings_form.h"
#include "tab5_runtime.h"



namespace tab5 {
void finishEdit();
void pressKey(size_t index);
lv_obj_t *controlHit(int16_t x, int16_t y);
void controlAction(lv_obj_t *control);
void controlCleanup();
void manualFinishConfirm();
void manualFinishDismiss();
namespace {
constexpr const char *kKeypadKeys[] = {"7", "8", "9", "DEL", "4", "5", "6", "CLR",
                                      "1", "2", "3", "SET", "-", "0", ".", ":"};
void text(lv_obj_t *o, const char *value) {
  if (o && strcmp(lv_label_get_text(o), value)) lv_label_set_text(o, value);
}
void enabled(lv_obj_t *o, bool value) {
  if (o) {
    if (value == !lv_obj_has_state(o, LV_STATE_DISABLED)) return;
    if (value)
      lv_obj_remove_state(o, LV_STATE_DISABLED);
    else
      lv_obj_add_state(o, LV_STATE_DISABLED);
  }
}
void visible(lv_obj_t *o, bool value) {
  if (o) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == !value) return;
    if (value)
      lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
}
void led(lv_obj_t *o, bool value) {
  if (!o) return;
  auto color = lv_color_hex(value ? 0x087F8C : 0xBCC6D0);
  if (!lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), color))
    lv_obj_set_style_bg_color(o, color, 0);
}
void checked(lv_obj_t *o, bool value) {
  if (!o) return;
  // Programmatic state updates do not emit VALUE_CHANGED.
  if (value)
    lv_obj_add_state(o, LV_STATE_CHECKED);
  else
    lv_obj_remove_state(o, LV_STATE_CHECKED);
}
struct PageWidgets {
  lv_obj_t *screen, *speed, *average, *lap, *total, *lap_time, *total_target, *lap_target;
  lv_obj_t *notice, *gps_status, *link, *plan_status, *race_status, *action, *detail,
      *clock, *ntp;
  lv_obj_t *power, *ignition, *lap_button, *lap_title, *lap_action, *heartbeat, *pulse, *gps,
      *marker, *marker_backing, *cancel_button;
};
#define PAGE(prefix, screen_name, action_obj, detail_obj) \
  {objects.screen_name,                                   \
   objects.prefix##speed_label,                           \
   objects.prefix##average_speed_label,                   \
   objects.prefix##lap_number_label,                      \
   objects.prefix##total_elapsed_label,                   \
   objects.prefix##lap_elapsed_label,                     \
   objects.prefix##total_target_label,                    \
   objects.prefix##lap_target_label,                      \
   objects.prefix##notice_label,                          \
   objects.prefix##gps_status_label,                      \
   objects.prefix##communication_status_label,            \
   objects.prefix##plan_status_label,                     \
   objects.prefix##race_status_label,                     \
   action_obj,                                            \
   detail_obj,                                            \
   objects.prefix##clock_label,                           \
   objects.prefix##ntp_status_label,                      \
   objects.prefix##electrical_standby_switch,             \
   objects.prefix##ignition_switch,                       \
   objects.prefix##manual_lap_button,                     \
   objects.prefix##manual_lap_button_text,                \
   objects.prefix##manual_lap_increment_label,           \
   objects.prefix##heartbeat_led,                         \
   objects.prefix##pulse_led,                             \
   objects.prefix##gps_led,                               \
   objects.prefix##position_marker,                       \
   objects.prefix##position_marker_backing,               \
   objects.prefix##cancel_timing_button}

ScreensEnum live_screen = SCREEN_ID_WAITING;
bool initialized = false;
lv_obj_t *gps_coordinate_labels[2][2]{};  // [Main, Waiting][latitude, longitude]
vega::Settings draft{};
size_t editing_field = 0;
bool save_pending = false;
vega::Settings pending_settings{};
bool ignition_pending = false;
bool finish_mode = false;
bool finish_ready = false;
bool finish_pending = false;
uint64_t finish_requested_ms = 0;
lv_obj_t *finish_overlay = nullptr;
PowerIntent power_intent;
uint32_t stale_control_presses = 0;
uint32_t power_taps = 0, ignition_taps = 0;
struct ControlPage {
  const char *name;
  lv_obj_t *screen, *power, *ignition;
};
std::array<ControlPage, 10> control_pages{};
std::array<lv_obj_t *, 10> ignition_progress_arcs{};
std::array<int16_t, 10> ignition_progress_angles{};
bool was_ignition_preparing = false;
bool ignition_finish_mask = false;
uint32_t ignition_finish_started = 0;
lv_obj_t *brightness_slider = nullptr;
lv_obj_t *brightness_value = nullptr;
bool refreshing_brightness = false;
uint64_t save_started = 0;
uint32_t pending_settings_attempt = 0;
char message[180] = "TARGET / MM:SS - COORDINATES / DEGREES";
constexpr int32_t kCourseMapSize = 480;
constexpr size_t kCourseMapCount = 10;
constexpr double kLapLineHalfLength = 22.0;
constexpr double kLapLineTangentSampleM = 10.0;
constexpr uint32_t kLapLineColor = 0x6F42C1;
struct CourseMarker {
  lv_obj_t *point = nullptr;
  lv_obj_t *label = nullptr;
};
struct CourseMapMarkers {
  CourseMarker start;
  CourseMarker goal;
  lv_obj_t *lap_line = nullptr;
  lv_obj_t *lap_legend = nullptr;
  lv_point_precise_t lap_points[2]{};
};
std::array<CourseMapMarkers, kCourseMapCount> course_markers{};
std::array<lv_obj_t *, kCourseMapCount> course_images{};
uint8_t displayed_course_index = 0;
const vega::CourseData &courseData() { return *courseAsset(displayed_course_index).data; }
lv_obj_t *selection_overlay = nullptr;
lv_obj_t *selection_list = nullptr;
lv_obj_t *selection_message = nullptr;
lv_obj_t *selection_refresh = nullptr;
lv_obj_t *course_buttons[8]{};
PlanChoices shown_choices{};
bool choices_initialized = false;
constexpr size_t kPlanLinePoints = 96;
struct PlanLine {
  lv_obj_t *object = nullptr;
  std::array<lv_point_precise_t, kPlanLinePoints> points{};
};
struct PlanMap {
  std::array<PlanLine, vega::kMaxStrategyRunsPerLap> acceleration{};
  std::array<PlanLine, vega::kMaxStrategyRunsPerLap + 1> coasting{};
  std::array<lv_obj_t *, vega::kMaxStrategyRunsPerLap> on_backing{}, on_flame{},
      off_square{};
  uint8_t shown_lap = 0;
  bool shown = false;
};
std::array<PlanMap, 2> plan_maps{};  // Main and Waiting preview.

lv_point_precise_t planPixel(const vega::CoursePath &path, double s_m) {
  const double distance = std::clamp(s_m, 0.0, path.length);
  size_t low = 0, high = path.count - 1;
  while (high - low > 1) {
    const size_t middle = (low + high) / 2;
    if (path.points[middle].s <= distance) low = middle;
    else high = middle;
  }
  const auto &a = path.points[low], &b = path.points[high];
  const double fraction = b.s > a.s ? (distance - a.s) / (b.s - a.s) : 0;
  const double east = a.east + fraction * (b.east - a.east);
  const double north = a.north + fraction * (b.north - a.north);
  const auto &matrix = courseData().pixel_matrix;
  return {static_cast<lv_value_precise_t>(std::lround(matrix[0] * east + matrix[1] * north + matrix[2])),
          static_cast<lv_value_precise_t>(std::lround(matrix[3] * east + matrix[4] * north + matrix[5]))};
}

void drawPlanLine(PlanLine &line, const vega::CoursePath &path,
                  double from_m, double to_m) {
  if (!line.object || to_m - from_m < 1) {
    visible(line.object, false);
    return;
  }
  const size_t count = std::min(kPlanLinePoints,
                                static_cast<size_t>(std::ceil((to_m - from_m) / 15)) + 2);
  for (size_t i = 0; i < count; ++i) {
    const double s_m = from_m + (to_m - from_m) * i / (count - 1);
    line.points[i] = planPixel(path, s_m);
  }
  lv_line_set_points(line.object, line.points.data(), count);
  visible(line.object, true);
}

void hidePlanMap(PlanMap &map) {
  for (auto &line : map.acceleration) visible(line.object, false);
  for (auto &line : map.coasting) visible(line.object, false);
  for (size_t i = 0; i < vega::kMaxStrategyRunsPerLap; ++i) {
    visible(map.on_backing[i], false);
    visible(map.on_flame[i], false);
    visible(map.off_square[i], false);
  }
  map.shown = false;
  map.shown_lap = 0;
}

void renderPlanMap(PlanMap &map, const vega::DisplayModel &model) {
  if (!model.plan_loaded) {
    if (map.shown) hidePlanMap(map);
    return;
  }
  if (map.shown && map.shown_lap == model.plan_lap_number) return;
  const auto &lap = model.plan_lap;
  const auto &path = courseData().routes[static_cast<size_t>(lap.route)];
  double previous_off = 0;
  for (size_t i = 0; i < vega::kMaxStrategyRunsPerLap; ++i) {
    if (i >= lap.run_count) {
      visible(map.acceleration[i].object, false);
      visible(map.coasting[i].object, false);
      visible(map.on_backing[i], false);
      visible(map.on_flame[i], false);
      visible(map.off_square[i], false);
      continue;
    }
    const auto &run = lap.runs[i];
    drawPlanLine(map.coasting[i], path, previous_off, run.on_s_m);
    drawPlanLine(map.acceleration[i], path, run.on_s_m, run.off_s_m);
    const auto on = planPixel(path, run.on_s_m);
    const auto off = planPixel(path, run.off_s_m);
    const int32_t on_x = on.x;
    const int32_t on_y = on.y;
    const int32_t off_x = off.x;
    const int32_t off_y = off.y;
    lv_obj_set_pos(map.on_backing[i], on_x - 22, on_y - 22);
    lv_obj_set_pos(map.on_flame[i], on_x - 20, on_y - 20);
    lv_obj_set_pos(map.off_square[i], off_x - 12, off_y - 12);
    visible(map.on_backing[i], true);
    visible(map.on_flame[i], true);
    visible(map.off_square[i], true);
    previous_off = run.off_s_m;
  }
  drawPlanLine(map.coasting[lap.run_count], path, previous_off, lap.route_length_m);
  for (size_t i = lap.run_count + 1; i < map.coasting.size(); ++i)
    visible(map.coasting[i].object, false);
  map.shown = true;
  map.shown_lap = model.plan_lap_number;
}

void createPlanMap(PlanMap &map, lv_obj_t *parent) {
  auto make_line = [parent](PlanLine &line, uint32_t color) {
    line.object = lv_line_create(parent);
    lv_obj_set_pos(line.object, 0, 0);
    lv_obj_set_size(line.object, kCourseMapSize, kCourseMapSize);
    lv_obj_remove_flag(line.object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_color(line.object, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(line.object, 8, 0);
    lv_obj_set_style_line_rounded(line.object, true, 0);
    visible(line.object, false);
  };
  for (auto &line : map.acceleration) make_line(line, 0xDB741D);
  for (auto &line : map.coasting) make_line(line, 0x1769B2);
  for (size_t i = 0; i < vega::kMaxStrategyRunsPerLap; ++i) {
    auto *backing = lv_obj_create(parent);
    lv_obj_set_size(backing, 44, 44);
    lv_obj_set_style_pad_all(backing, 0, 0);
    lv_obj_set_style_border_width(backing, 0, 0);
    lv_obj_set_style_radius(backing, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(backing, lv_color_hex(0xDB741D), 0);
    lv_obj_set_style_bg_opa(backing, LV_OPA_COVER, 0);
    lv_obj_remove_flag(backing, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(backing, LV_OBJ_FLAG_SCROLLABLE);
    map.on_backing[i] = backing;
    auto *flame = lv_image_create(parent);
    lv_obj_set_size(flame, 40, 40);
    lv_image_set_src(flame, &img_course_ignition_flame);
    lv_obj_remove_flag(flame, LV_OBJ_FLAG_CLICKABLE);
    map.on_flame[i] = flame;
    auto *square = lv_obj_create(parent);
    lv_obj_set_size(square, 24, 24);
    lv_obj_set_style_pad_all(square, 0, 0);
    lv_obj_set_style_border_width(square, 0, 0);
    lv_obj_set_style_radius(square, 2, 0);
    lv_obj_set_style_bg_color(square, lv_color_hex(0x1769B2), 0);
    lv_obj_remove_flag(square, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(square, LV_OBJ_FLAG_SCROLLABLE);
    map.off_square[i] = square;
  }
  hidePlanMap(map);
}
vega::GeoPoint displayed_start{};
vega::GeoPoint displayed_goal{};
vega::GeoPoint displayed_timing{};
double displayed_corridor_m = 0;
bool course_markers_positioned = false;

lv_obj_t *createCourseLegend(lv_obj_t *parent, const char *caption, uint32_t color,
                             int32_t legend_x) {
  auto *label = lv_label_create(parent);
  // A shared footer stays below the mapped routes on every course artwork.
  lv_obj_set_pos(label, legend_x, 454);
  lv_obj_set_size(label, 100, 26);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(label, 0, 0);
  lv_obj_set_style_radius(label, 7, 0);
  lv_obj_set_style_bg_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(label, 0, 0);
  lv_obj_set_style_shadow_width(label, 0, 0);
  lv_obj_set_style_text_font(label, &ui_font_ricty_diminished_24, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(label, 1, 0);
  lv_label_set_text_static(label, caption);
  return label;
}

CourseMarker createCourseMarker(lv_obj_t *parent, const char *caption, uint32_t color,
                                int32_t legend_x) {
  CourseMarker marker;
  marker.point = lv_obj_create(parent);
  lv_obj_set_size(marker.point, 18, 18);
  lv_obj_remove_flag(marker.point, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(marker.point, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(marker.point, 0, 0);
  lv_obj_set_style_radius(marker.point, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(marker.point, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(marker.point, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(marker.point, lv_color_white(), 0);
  lv_obj_set_style_border_width(marker.point, 2, 0);
  lv_obj_set_style_shadow_width(marker.point, 0, 0);
  marker.label = createCourseLegend(parent, caption, color, legend_x);
  return marker;
}

void positionCourseMarker(CourseMarker &marker, const vega::MapPosition &position) {
  const bool on_map = std::isfinite(position.x) && std::isfinite(position.y) &&
                      position.x >= 0 && position.x < kCourseMapSize && position.y >= 0 &&
                      position.y < kCourseMapSize;
  visible(marker.point, on_map);
  visible(marker.label, on_map);
  if (!on_map) return;
  const int32_t x = static_cast<int32_t>(std::lround(position.x));
  const int32_t y = static_cast<int32_t>(std::lround(position.y));
  lv_obj_set_pos(marker.point, x - 9, y - 9);
}

bool lapLinePoints(const vega::Course &course, const vega::Settings &settings,
                   lv_point_precise_t (&points)[2]) {
  const auto timing = course.locate(settings.timing, settings.course_corridor_m);
  if (!timing.on_course) return false;
  const auto center = course.locate(course.pointAt(timing.s_m), settings.course_corridor_m);
  const auto before = course.locate(course.pointAt(timing.s_m - kLapLineTangentSampleM),
                                    settings.course_corridor_m);
  const auto after = course.locate(course.pointAt(timing.s_m + kLapLineTangentSampleM),
                                   settings.course_corridor_m);
  if (!std::isfinite(center.x) || !std::isfinite(center.y) || center.x < 0 ||
      center.x >= kCourseMapSize || center.y < 0 || center.y >= kCourseMapSize)
    return false;
  const double dx = after.x - before.x;
  const double dy = after.y - before.y;
  const double length = std::hypot(dx, dy);
  if (!std::isfinite(length) || length < 1) return false;
  const double normal_x = -dy / length;
  const double normal_y = dx / length;
  points[0].x = center.x - kLapLineHalfLength * normal_x;
  points[0].y = center.y - kLapLineHalfLength * normal_y;
  points[1].x = center.x + kLapLineHalfLength * normal_x;
  points[1].y = center.y + kLapLineHalfLength * normal_y;
  return true;
}

void updateCourseMarkers(const vega::Settings &settings) {
  if (course_markers_positioned && settings.start.latitude == displayed_start.latitude &&
      settings.start.longitude == displayed_start.longitude &&
      settings.goal.latitude == displayed_goal.latitude &&
      settings.goal.longitude == displayed_goal.longitude &&
      settings.timing.latitude == displayed_timing.latitude &&
      settings.timing.longitude == displayed_timing.longitude &&
      settings.course_corridor_m == displayed_corridor_m)
    return;
  vega::Course course(courseData());
  const auto start = course.locateOn(settings.start, settings.course_corridor_m,
                                      vega::CourseRoute::First);
  const auto goal = course.locateOn(settings.goal, settings.course_corridor_m,
                                     vega::CourseRoute::FinishApproach);
  lv_point_precise_t lap_points[2]{};
  const bool lap_line_visible = lapLinePoints(course, settings, lap_points);
  for (auto &map : course_markers) {
    positionCourseMarker(map.start, start);
    positionCourseMarker(map.goal, goal);
    visible(map.lap_line, lap_line_visible);
    visible(map.lap_legend, lap_line_visible);
    if (lap_line_visible) {
      map.lap_points[0] = lap_points[0];
      map.lap_points[1] = lap_points[1];
      lv_line_set_points(map.lap_line, map.lap_points, 2);
      if (lv_obj_get_x(map.lap_line) != 0 || lv_obj_get_y(map.lap_line) != 0)
        lv_obj_set_pos(map.lap_line, 0, 0);
    }
  }
  displayed_start = settings.start;
  displayed_goal = settings.goal;
  displayed_timing = settings.timing;
  displayed_corridor_m = settings.course_corridor_m;
  course_markers_positioned = true;
}

lv_obj_t *fieldButton(size_t i) {
  lv_obj_t *fields[] = {objects.settings_total_button,      objects.settings_lap1_button,
                        objects.settings_lap2_button,       objects.settings_lap3_button,
                        objects.settings_lap4_button,       objects.settings_lap5_button,
                        objects.settings_lap6_button,       objects.settings_lap7_button,
                        objects.settings_start_lat_button,  objects.settings_start_lon_button,
                        objects.settings_timing_lat_button, objects.settings_timing_lon_button,
                        objects.settings_goal_lat_button,   objects.settings_goal_lon_button,
                        objects.settings_advanced_wheel_circ_button,
                        objects.settings_advanced_pulses_per_rev_button,
                        objects.settings_advanced_ecu_ready_button,
                        objects.settings_advanced_ignition_pulse_button,
                        objects.settings_advanced_power_high_button,
                        objects.settings_advanced_debounce_button,
                        objects.settings_advanced_zero_speed_button,
                        objects.settings_advanced_gps_stale_button,
                        objects.settings_advanced_corridor_button,
                        objects.settings_advanced_max_gps_step_button,
                        objects.settings_advanced_min_lap_dist_button,
                        objects.settings_advanced_min_lap_time_button,
                        objects.settings_advanced_lap_duplicate_button};
  return i < vega::kSettingsFieldCount ? fields[i] : nullptr;
}
void refreshFields() {
  for (size_t i = 0; i < vega::kSettingsFieldCount; ++i) {
    char value[32];
    vega::settingText(draft, i, value, sizeof(value));
    text(lv_obj_get_child(fieldButton(i), 0), value);
  }
}
void refreshGpsSourceSelector() {
  const bool port_a = draft.gps_source == vega::GpsSource::PortA;
  lv_obj_set_style_bg_color(objects.settings_advanced_gps_m5bus_button,
                            lv_color_hex(port_a ? 0x64748B : 0x1769B2), 0);
  lv_obj_set_style_bg_color(objects.settings_advanced_gps_port_a_button,
                            lv_color_hex(port_a ? 0x1769B2 : 0x64748B), 0);
}
void setMessage(const char *value) {
  std::snprintf(message, sizeof(message), "%s", value);
  text(objects.settings_message_label, message);
  text(objects.settings_advanced_message_label, message);
  text(objects.settings_general_message_label, message);
}
bool sameSettings(const vega::Settings &a, const vega::Settings &b) {
  return a.version == b.version && a.gps_source == b.gps_source &&
         a.display_brightness == b.display_brightness &&
         a.total_target_s == b.total_target_s &&
         a.lap_target_s == b.lap_target_s && a.start.latitude == b.start.latitude &&
         a.start.longitude == b.start.longitude && a.timing.latitude == b.timing.latitude &&
         a.timing.longitude == b.timing.longitude && a.goal.latitude == b.goal.latitude &&
         a.goal.longitude == b.goal.longitude &&
         a.wheel_circumference_m == b.wheel_circumference_m &&
         a.pulses_per_revolution == b.pulses_per_revolution &&
         a.ecu_ready_ms == b.ecu_ready_ms && a.ignition_pulse_ms == b.ignition_pulse_ms &&
         a.power_active_high == b.power_active_high && a.pulse_debounce_us == b.pulse_debounce_us &&
         a.speed_zero_ms == b.speed_zero_ms && a.gps_stale_ms == b.gps_stale_ms &&
         a.course_corridor_m == b.course_corridor_m && a.max_gps_step_m == b.max_gps_step_m &&
         a.min_lap_progress_m == b.min_lap_progress_m && a.min_lap_ms == b.min_lap_ms &&
         a.lap_duplicate_ms == b.lap_duplicate_ms;
}
void refreshBrightnessControl() {
  if (!brightness_slider) return;
  refreshing_brightness = true;
  lv_slider_set_value(brightness_slider, draft.display_brightness, LV_ANIM_OFF);
  refreshing_brightness = false;
  char value[16];
  std::snprintf(value, sizeof(value), "%lu%%",
                static_cast<unsigned long>((draft.display_brightness * 100 + 127) / 255));
  text(brightness_value, value);
}
class View final : public vega::IView {
 public:
  void show(const vega::DisplayModel &m) override {
    for (auto &page : gps_coordinate_labels) {
      text(page[0], m.gps_latitude);
      text(page[1], m.gps_longitude);
    }
    power_intent.observe(m.power_on);
    PageWidgets pages[] = {
        PAGE(, main, objects.next_action_label, objects.next_action_detail_label),
        PAGE(waiting_, waiting, nullptr, nullptr),
        PAGE(finished_, finished, objects.finished_next_action_label,
             objects.finished_next_action_detail_label)};
    auto next = m.phase == vega::RacePhase::Waiting    ? SCREEN_ID_WAITING
                : m.phase == vega::RacePhase::Finished ? SCREEN_ID_FINISHED
                                                       : SCREEN_ID_MAIN;
    bool changed = !initialized || live_screen != next;
    live_screen = next;
    auto active = lv_screen_active();
    if (changed && (!initialized || active == objects.main || active == objects.waiting ||
                    active == objects.finished))
      loadScreen(live_screen);
    initialized = true;
    const bool settings_preview = lv_screen_active() == objects.settings &&
                                  m.phase != vega::RacePhase::Measuring;
    tab5_lvgl_set_brightness(static_cast<uint8_t>(settings_preview ? draft.display_brightness
                                                                 : m.display_brightness));
    if (!m.ignition_enabled) ignition_pending = false;
    enabled(objects.start_button, m.phase == vega::RacePhase::Waiting);
    static lv_point_precise_t marker_points[3][5];
    static bool marker_points_initialized[3]{};
    size_t page_index = 0;
    // Keep all live page copies consistent, including off-screen controls.
    for (auto &p : pages) {
      text(p.speed, m.speed);
      text(p.average, m.average);
      text(p.lap, m.lap);
      text(p.total, m.total);
      text(p.lap_time, m.lap_time);
      text(p.total_target, m.total_target);
      text(p.lap_target, m.lap_target);
      text(p.notice, m.notice);
      text(p.gps_status, m.gps_status);
      text(p.link, m.link);
      text(p.plan_status, m.plan_status);
      text(p.race_status, m.race_status);
      text(p.action, m.action);
      text(p.detail, m.detail);
      text(p.clock, m.clock);
      text(p.ntp, m.ntp);
      checked(p.power, m.power_on);
      enabled(p.ignition, m.ignition_enabled && !ignition_pending);
      enabled(p.lap_button, m.lap_enabled && !finish_pending);
      text(p.lap_title, m.finish_mode ? "GOAL" : "MANUAL");
      text(p.lap_action, m.finish_mode ? "FINISH" : "LAP +1");
      const auto lap_color = lv_color_hex(m.finish_mode ? 0xB43832 : 0x1769B2);
      if (!lv_color_eq(lv_obj_get_style_bg_color(p.lap_button, LV_PART_MAIN), lap_color))
        lv_obj_set_style_bg_color(p.lap_button, lap_color, LV_STATE_DEFAULT);
      enabled(p.cancel_button, m.phase == vega::RacePhase::Measuring);
      led(p.heartbeat, m.heartbeat);
      led(p.pulse, m.pulse);
      led(p.gps, m.gps_ok);
      visible(p.marker, m.position_visible);
      visible(p.marker_backing, m.position_visible);
      if (m.position_visible) {
        if (lv_obj_get_x(p.marker) != 0 || lv_obj_get_y(p.marker) != 0)
          lv_obj_set_pos(p.marker, 0, 0);
        int32_t backing_x = m.marker_x - lv_obj_get_width(p.marker_backing) / 2;
        int32_t backing_y = m.marker_y - lv_obj_get_height(p.marker_backing) / 2;
        if (lv_obj_get_x(p.marker_backing) != backing_x ||
            lv_obj_get_y(p.marker_backing) != backing_y)
          lv_obj_set_pos(p.marker_backing, backing_x, backing_y);
        // Rotate a north-pointing arrow into the reported GPS course. Coordinates
        // stay in the course container and are never snapped onto the route.
        constexpr double shape[5][2] = {{0, -15}, {12, 12}, {0, 6}, {-12, 12}, {0, -15}};
        double angle = m.marker_heading * 3.14159265358979323846 / 180;
        lv_point_precise_t next_points[5]{};
        for (size_t i = 0; i < 5; ++i) {
          next_points[i].x =
              m.marker_x + shape[i][0] * std::cos(angle) - shape[i][1] * std::sin(angle);
          next_points[i].y =
              m.marker_y + shape[i][0] * std::sin(angle) + shape[i][1] * std::cos(angle);
        }
        if (!marker_points_initialized[page_index] ||
            std::memcmp(marker_points[page_index], next_points, sizeof(next_points))) {
          std::memcpy(marker_points[page_index], next_points, sizeof(next_points));
          lv_line_set_points(p.marker, marker_points[page_index], 5);
          marker_points_initialized[page_index] = true;
        }
        auto opacity = static_cast<lv_opa_t>(m.position_stale ? 100 : 255);
        if (lv_obj_get_style_opa(p.marker, LV_PART_MAIN) != opacity)
          lv_obj_set_style_opa(p.marker, opacity, 0);
        if (lv_obj_get_style_opa(p.marker_backing, LV_PART_MAIN) != opacity)
          lv_obj_set_style_opa(p.marker_backing, opacity, 0);
        // Preserve the established marker geometry; course position is raw GPS.
      }
      auto total_color = lv_color_hex(m.overtime ? 0xB43832 : 0x202B36);
      if (!lv_color_eq(lv_obj_get_style_text_color(p.total, LV_PART_MAIN), total_color))
        lv_obj_set_style_text_color(p.total, total_color, 0);
      ++page_index;
    }
    for (auto &map : plan_maps) renderPlanMap(map, m);
    if (m.ignition_preparing) {
      ignition_finish_mask = false;
    } else if (was_ignition_preparing && m.ignition_enabled) {
      // Cover LVGL's disabled-to-enabled color transition with the completed
      // red disc. The default theme fades for 80 ms after a 70 ms delay.
      ignition_finish_mask = true;
      ignition_finish_started = lv_tick_get();
    } else if (!m.power_on ||
               (ignition_finish_mask && lv_tick_elaps(ignition_finish_started) >= 200)) {
      ignition_finish_mask = false;
    }
    was_ignition_preparing = m.ignition_preparing;
    const bool show_ignition_arc = m.ignition_preparing || ignition_finish_mask;
    for (size_t i = 0; i < control_pages.size(); ++i) {
      checked(control_pages[i].power, m.power_on);
      auto *ignition = control_pages[i].ignition;
      // Keep the 3 px border in the layout so its flame child does not move.
      const lv_opa_t border_opa = show_ignition_arc ? LV_OPA_TRANSP : LV_OPA_COVER;
      if (lv_obj_get_style_border_opa(ignition, LV_PART_MAIN) != border_opa)
        lv_obj_set_style_border_opa(ignition, border_opa, LV_PART_MAIN);
      auto *arc = ignition_progress_arcs[i];
      if (!arc) continue;
      visible(arc, show_ignition_arc);
      if (show_ignition_arc) {
        const int16_t angle = m.ignition_preparing
                                  ? static_cast<int16_t>(std::min<uint32_t>(
                                        360, (uint32_t(m.ignition_prepare_permille) * 360 + 500) /
                                                 1000))
                                  : 360;
        if (ignition_progress_angles[i] != angle) {
          lv_arc_set_angles(arc, 0, angle);
          ignition_progress_angles[i] = angle;
        }
      } else {
        ignition_progress_angles[i] = -1;
      }
    }
    finish_mode = m.finish_mode;
    finish_ready = m.lap_enabled;
    if (!finish_mode || m.phase != vega::RacePhase::Measuring) {
      finish_pending = false;
      visible(finish_overlay, false);
    }
    bool editable = m.phase != vega::RacePhase::Measuring && !save_pending;
    enabled(brightness_slider, editable);
    for (size_t i = 0; i < vega::kSettingsFieldCount; ++i) enabled(fieldButton(i), editable);
    enabled(objects.settings_advanced_gps_m5bus_button, editable);
    enabled(objects.settings_advanced_gps_port_a_button, editable);
    for (size_t i = 16; i <= 18; ++i) enabled(fieldButton(i), editable && !m.power_on);
    enabled(objects.settings_save_button, editable);
    enabled(objects.settings_advanced_save_button, editable);
    enabled(objects.settings_general_save_button, editable);
    enabled(objects.settings_cancel_button, m.phase == vega::RacePhase::Measuring);
    enabled(objects.settings_advanced_cancel_button, m.phase == vega::RacePhase::Measuring);
    enabled(objects.settings_general_cancel_button, m.phase == vega::RacePhase::Measuring);
    if (m.phase == vega::RacePhase::Measuring && lv_screen_active() == objects.settings &&
        !save_pending) {
      setMessage("TIMING ACTIVE - SETTINGS LOCKED");
      visible(objects.settings_editor_overlay, false);
    }
  }
} view;
struct Sink final : vega::ICommandSink {
  bool submit(const vega::Command &c) override { return tab5::submit(c); }
} sink;
vega::Presenter presenter(view, sink, vega::Course(*courseAsset(0).data).lapCount());

bool request(CommandKind kind) {
  Command command{};
  command.kind = kind;
  if (!presenter.request(command)) {
    setMessage("COMMAND QUEUE FULL - TRY AGAIN");
    return false;
  }
  return true;
}
lv_obj_t *selectorButton(lv_obj_t *parent, int x, int y, int w, int h,
                         const char *caption, uint32_t color) {
  auto *button = lv_button_create(parent);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, w, h);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
  lv_obj_set_style_radius(button, 10, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  auto *label = lv_label_create(button);
  lv_obj_set_width(label, w - 20);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_label_set_text(label, caption);
  lv_obj_set_style_text_font(label, &ui_font_ricty_diminished_24, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_center(label);
  return button;
}
void refreshSelectionList(const PlanChoices &choices) {
  if (!selection_list) return;
  if (choices_initialized && memcmp(&shown_choices, &choices, sizeof(choices)) == 0) return;
  shown_choices = choices;
  choices_initialized = true;
  lv_obj_clean(selection_list);
  if (choices.course_index != displayed_course_index) {
    text(selection_message, "LOADING STRATEGIES...");
    return;
  }
  text(selection_message, choices.message[0] ? choices.message :
       "SELECT A STRATEGY FOR THIS COURSE");
  for (uint8_t i = 0; i < choices.count; ++i) {
    const auto &item = choices.items[i];
    char label[100];
    if (item.valid)
      snprintf(label, sizeof(label), "%s%s", i == choices.selected ? "[SELECTED] " : "", item.label);
    else
      snprintf(label, sizeof(label), "%s  (%s)", item.label, item.error);
    auto *button = selectorButton(selection_list, 8, 8 + i * 60, 520, 52, label,
                                  !item.valid ? 0x8996A3 : i == choices.selected ? 0x087F8C : 0x1769B2);
    if (!item.valid) lv_obj_add_state(button, LV_STATE_DISABLED);
    lv_obj_add_event_cb(button, [](lv_event_t *event) {
      vega::Snapshot current{};
      if (!snapshot(current) || current.race.phase != vega::RacePhase::Waiting ||
          current.engine != vega::EnginePhase::Off) {
        text(selection_message, "ELECTRICAL OFF TO CHANGE SELECTION");
        return;
      }
      const auto index = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
      Command command{};
      command.kind = CommandKind::SelectStrategy;
      command.choice = index;
      if (!presenter.request(command)) text(selection_message, "SELECTION REJECTED");
      else visible(selection_overlay, false);
    }, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
  }
}
void openSelection() {
  vega::Snapshot s{};
  if (!snapshot(s) || s.race.phase != vega::RacePhase::Waiting) return;
  action_close_menu(nullptr);
  visible(selection_overlay, true);
  lv_obj_move_foreground(selection_overlay);
  if (s.engine == vega::EnginePhase::Off) request(CommandKind::RefreshStrategies);
  else text(selection_message, "ELECTRICAL OFF TO CHANGE SELECTION");
}
void setupSelectionUi() {
  selection_overlay = lv_obj_create(objects.waiting);
  lv_obj_set_pos(selection_overlay, 0, 0);
  lv_obj_set_size(selection_overlay, 1280, 720);
  lv_obj_remove_flag(selection_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(selection_overlay, lv_color_hex(0xE6EDF4), 0);
  lv_obj_set_style_border_width(selection_overlay, 0, 0);
  lv_obj_set_style_pad_all(selection_overlay, 0, 0);
  auto *back = lv_button_create(selection_overlay);
  lv_obj_set_pos(back, 16, 12);
  lv_obj_set_size(back, 56, 56);
  lv_obj_set_style_pad_all(back, 0, 0);
  lv_obj_set_style_border_width(back, 0, 0);
  lv_obj_set_style_radius(back, 12, 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_set_style_bg_color(back, lv_color_hex(0xF0F4F8), LV_PART_MAIN);
  lv_obj_set_style_bg_color(back, lv_color_hex(0xDCE5EF), LV_STATE_PRESSED);
  lv_obj_add_event_cb(back, [](lv_event_t *) { visible(selection_overlay, false); },
                      LV_EVENT_CLICKED, nullptr);
  auto *back_icon = lv_image_create(back);
  lv_obj_set_pos(back_icon, 12, 12);
  lv_obj_set_size(back_icon, 32, 32);
  lv_image_set_src(back_icon, &img_arrow_left_dark);
  lv_obj_remove_flag(back_icon, LV_OBJ_FLAG_CLICKABLE);
  auto *title = lv_label_create(selection_overlay);
  lv_obj_set_pos(title, 96, 26);
  lv_obj_set_size(title, 760, 43);
  lv_label_set_text_static(title, "COURSE / STRATEGY");
  lv_obj_set_style_text_font(title, &ui_font_ricty_diminished_48, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x202B36), 0);
  selection_refresh = selectorButton(selection_overlay, 902, 16, 174, 56, "REFRESH", 0x1769B2);
  lv_obj_add_event_cb(selection_refresh, [](lv_event_t *) { request(CommandKind::RefreshStrategies); },
                      LV_EVENT_CLICKED, nullptr);
  auto *course_title = lv_label_create(selection_overlay);
  lv_obj_set_pos(course_title, 42, 96);
  lv_label_set_text_static(course_title, "COURSE");
  lv_obj_set_style_text_font(course_title, &ui_font_ricty_diminished_24, 0);
  for (size_t i = 0; i < courseCount() && i < 8; ++i) {
    course_buttons[i] = selectorButton(selection_overlay, 42, 140 + i * 72, 430, 62,
                                       courseAsset(i).name, 0x1769B2);
    lv_obj_add_event_cb(course_buttons[i], [](lv_event_t *event) {
      vega::Snapshot current{};
      if (!snapshot(current) || current.engine != vega::EnginePhase::Off) {
        text(selection_message, "ELECTRICAL OFF TO CHANGE SELECTION");
        return;
      }
      Command command{};
      command.kind = CommandKind::SelectCourse;
      command.choice = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
      if (!presenter.request(command)) text(selection_message, "COURSE CHANGE REJECTED");
    }, LV_EVENT_CLICKED, reinterpret_cast<void *>(i));
  }
  auto *plan_title = lv_label_create(selection_overlay);
  lv_obj_set_pos(plan_title, 536, 96);
  lv_label_set_text_static(plan_title, "STRATEGY ON microSD");
  lv_obj_set_style_text_font(plan_title, &ui_font_ricty_diminished_24, 0);
  selection_list = lv_obj_create(selection_overlay);
  lv_obj_set_pos(selection_list, 528, 132);
  lv_obj_set_size(selection_list, 568, 474);
  lv_obj_set_style_pad_all(selection_list, 0, 0);
  selection_message = lv_label_create(selection_overlay);
  lv_obj_set_pos(selection_message, 42, 640);
  lv_obj_set_size(selection_message, 1160, 32);
  lv_label_set_text_static(selection_message, "LOADING STRATEGIES...");
  lv_obj_set_style_text_font(selection_message, &ui_font_ricty_diminished_24, 0);
  visible(selection_overlay, false);
}
}  // namespace

void viewOpenSelection() { openSelection(); }

void viewBegin() {
  // Both live pages have the same free space between race status and clock.
  auto createGpsCoordinateLabel = [](lv_obj_t *screen, int y, const char *initial) {
    lv_obj_t *label = lv_label_create(screen);
    lv_obj_set_pos(label, 780, y);
    lv_obj_set_size(label, 210, 24);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(label, &ui_font_ricty_diminished_24, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x596775), 0);
    lv_label_set_text_static(label, initial);
    return label;
  };
  lv_obj_t *live_screens[] = {objects.main, objects.waiting};
  for (size_t i = 0; i < 2; ++i) {
    gps_coordinate_labels[i][0] = createGpsCoordinateLabel(live_screens[i], 16, "LAT --");
    gps_coordinate_labels[i][1] = createGpsCoordinateLabel(live_screens[i], 48, "LON --");
  }

  control_pages = {{{"main", objects.main, objects.electrical_standby_switch,
                     objects.ignition_switch},
                    {"waiting", objects.waiting, objects.waiting_electrical_standby_switch,
                     objects.waiting_ignition_switch},
                    {"finished", objects.finished, objects.finished_electrical_standby_switch,
                     objects.finished_ignition_switch},
                    {"gps_stale", objects.gps_stale, objects.gpsstale_electrical_standby_switch,
                     objects.gpsstale_ignition_switch},
                    {"missing_data", objects.missing_data,
                     objects.missingdata_electrical_standby_switch,
                     objects.missingdata_ignition_switch},
                    {"overtime", objects.overtime, objects.overtime_electrical_standby_switch,
                     objects.overtime_ignition_switch},
                    {"plan_demo", objects.plan_demo, objects.plandemo_electrical_standby_switch,
                     objects.plandemo_ignition_switch},
                    {"cached_plan", objects.cached_plan,
                     objects.cachedplan_electrical_standby_switch,
                     objects.cachedplan_ignition_switch},
                    {"expired_plan", objects.expired_plan,
                     objects.expiredplan_electrical_standby_switch,
                     objects.expiredplan_ignition_switch},
                    {"lap_corrected", objects.lap_corrected,
                     objects.lapcorrected_electrical_standby_switch,
                     objects.lapcorrected_ignition_switch}}};
  for (size_t i = 0; i < control_pages.size(); ++i) {
    const auto &page = control_pages[i];
    // The model owns CHECKED. Commands use complete touch gestures rather than
    // LVGL's RELEASED/CLICKED, which can be lost after an unrelated indev reset.
    lv_obj_remove_flag(page.power, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_remove_event_cb(page.power, action_electrical_changed);
    lv_obj_remove_event_cb(page.ignition, action_ignite);
    // Match the disabled button's visible color, then reveal its enabled red
    // clockwise from 12 o'clock. The child arc must not inherit the disabled
    // color filter, or the filled portion would stay pale too.
    auto *flame = lv_obj_get_child(page.ignition, 0);
    const auto ready_color = lv_obj_get_style_bg_color(page.ignition, LV_PART_MAIN);
    // LVGL's light default theme gives disabled buttons this 50% grey mix.
    // Compute it directly, independent of when the button style is resolved.
    const auto waiting_color =
        lv_color_mix(lv_palette_lighten(LV_PALETTE_GREY, 2), ready_color, LV_OPA_50);
    auto *arc = lv_arc_create(page.ignition);
    ignition_progress_arcs[i] = arc;
    ignition_progress_angles[i] = -1;
    // The progress disc covers the whole 208 px button, including its border.
    // Center alignment avoids an offset from the parent's content origin.
    lv_obj_set_size(arc, 208, 208);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(arc, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                    LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    lv_obj_set_style_color_filter_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_color_filter_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, waiting_color, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, ready_color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 104, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 104, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 0);
    lv_arc_set_rotation(arc, 270);
    visible(arc, false);
    if (flame) lv_obj_move_foreground(flame);
  }
  tab5_lvgl_set_control_handlers(controlHit, controlAction, controlCleanup);
  brightness_slider = lv_slider_create(objects.settings_brightness_control);
  lv_obj_set_pos(brightness_slider, 4, 0);
  lv_obj_set_size(brightness_slider, 204, 46);
  lv_slider_set_range(brightness_slider, vega::kMinDisplayBrightness,
                      vega::kMaxDisplayBrightness);
  lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0xDCE5EF), LV_PART_MAIN);
  lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x1769B2), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x1769B2), LV_PART_KNOB);
  lv_obj_add_event_cb(brightness_slider,
                      [](lv_event_t *event) {
                        if (refreshing_brightness || save_pending ||
                            presenter.phase() == vega::RacePhase::Measuring)
                          return;
                        draft.display_brightness =
                            lv_slider_get_value(lv_event_get_target_obj(event));
                        refreshBrightnessControl();
                        tab5_lvgl_set_brightness(static_cast<uint8_t>(draft.display_brightness));
                        setMessage("BRIGHTNESS PREVIEW - SAVE SETTINGS");
                      },
                      LV_EVENT_VALUE_CHANGED, nullptr);
  brightness_value = lv_label_create(objects.settings_brightness_control);
  lv_obj_set_pos(brightness_value, 215, 8);
  lv_obj_set_size(brightness_value, 59, 30);
  lv_label_set_long_mode(brightness_value, LV_LABEL_LONG_CLIP);
  lv_obj_remove_flag(brightness_value, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_text_font(brightness_value, &ui_font_ricty_diminished_24, 0);
  lv_obj_set_style_text_color(brightness_value, lv_color_hex(0x202B36), 0);
  refreshBrightnessControl();
  // A full-screen confirmation prevents an accidental second touch from
  // finishing the race. The electrical OFF gesture is still handled below.
  finish_overlay = lv_obj_create(lv_display_get_layer_top(lv_display_get_default()));
  lv_obj_set_pos(finish_overlay, 0, 0);
  lv_obj_set_size(finish_overlay, 1280, 720);
  lv_obj_remove_flag(finish_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(finish_overlay, 0, 0);
  lv_obj_set_style_border_width(finish_overlay, 0, 0);
  lv_obj_set_style_radius(finish_overlay, 0, 0);
  lv_obj_set_style_bg_color(finish_overlay, lv_color_hex(0xF0F4F8), 0);
  visible(finish_overlay, false);
  auto *title = lv_label_create(finish_overlay);
  lv_obj_set_pos(title, 150, 210);
  lv_obj_set_size(title, 1000, 64);
  lv_obj_set_style_text_font(title, &ui_font_ricty_diminished_48, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x202B36), 0);
  lv_label_set_text_static(title, "FINISH TIMING?");
  auto *detail = lv_label_create(finish_overlay);
  lv_obj_set_pos(detail, 150, 290);
  lv_obj_set_size(detail, 1000, 60);
  lv_obj_set_style_text_font(detail, &ui_font_ricty_diminished_32, 0);
  lv_obj_set_style_text_color(detail, lv_color_hex(0x202B36), 0);
  lv_label_set_text_static(detail, "Finalize the result at the current time?");
  auto add_finish_button = [](int32_t x, const char *caption, uint32_t color,
                              lv_event_cb_t callback) {
    auto *button = lv_button_create(finish_overlay);
    lv_obj_set_pos(button, x, 410);
    lv_obj_set_size(button, 420, 96);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    auto *label = lv_label_create(button);
    lv_label_set_text_static(label, caption);
    lv_obj_set_style_text_font(label, &ui_font_ricty_diminished_32, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
  };
  add_finish_button(170, "FINISH TIMING", 0xB43832,
                    [](lv_event_t *) { manualFinishConfirm(); });
  add_finish_button(680, "KEEP TIMING", 0x1769B2,
                    [](lv_event_t *) { manualFinishDismiss(); });
  // lv_textarea_set_one_line() replaces the EEZ height with LV_SIZE_CONTENT.
  // Restore it so the input and SET button share the same bottom edge.
  lv_obj_set_height(objects.settings_editor_input, 76);
  // The generated LVGL keyboard renders blank on Tab5. Keep its EEZ layout slot,
  // but use ordinary buttons for the keys so they are drawn and hit-tested like
  // the other controls on this screen.
  // The Settings screen has not been laid out yet, so lv_obj_get_width/height
  // can still return zero here. These match settings_keyboard in the EEZ file.
  constexpr int32_t x = 24;
  constexpr int32_t y = 264;
  constexpr int32_t width = 1232;
  constexpr int32_t height = 424;
  visible(objects.settings_keyboard, false);
  auto *keypad = lv_obj_create(objects.settings_editor_overlay);
  lv_obj_set_pos(keypad, x, y);
  lv_obj_set_size(keypad, width, height);
  lv_obj_remove_flag(keypad, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(keypad, 0, 0);
  lv_obj_set_style_border_width(keypad, 0, 0);
  lv_obj_set_style_shadow_width(keypad, 0, 0);
  lv_obj_set_style_bg_color(keypad, lv_color_hex(0xE6EDF4), 0);
  lv_obj_set_style_radius(keypad, 8, 0);
  constexpr int32_t margin = 8;
  constexpr int32_t gap = 8;
  const int32_t key_width = (width - 2 * margin - 3 * gap) / 4;
  const int32_t key_height = (height - 2 * margin - 3 * gap) / 4;
  for (size_t i = 0; i < sizeof(kKeypadKeys) / sizeof(kKeypadKeys[0]); ++i) {
    const bool action = i == 3 || i == 7 || i == 11;
    const uint32_t color = i == 11 ? 0x1769B2 : action ? 0x64748B : 0xFFFFFF;
    auto *button = lv_button_create(keypad);
    lv_obj_set_pos(button, margin + (i % 4) * (key_width + gap),
                   margin + (i / 4) * (key_height + gap));
    lv_obj_set_size(button, key_width, key_height);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(action ? 0x14558F : 0xDCE5EF),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button,
                        [](lv_event_t *e) {
                          pressKey(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
                        },
                        LV_EVENT_CLICKED, reinterpret_cast<void *>(i));
    auto *label = lv_label_create(button);
    lv_label_set_text_static(label, kKeypadKeys[i]);
    lv_obj_set_style_text_font(label, &ui_font_ricty_diminished_32, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(action ? 0xFFFFFF : 0x202B36), 0);
    lv_obj_center(label);
  }
  lv_obj_t *maps[kCourseMapCount] = {
      objects.course_container,            objects.waiting_course_container,
      objects.finished_course_container,   objects.gpsstale_course_container,
      objects.missingdata_course_container, objects.overtime_course_container,
      objects.plandemo_course_container,   objects.cachedplan_course_container,
      objects.expiredplan_course_container, objects.lapcorrected_course_container};
  for (size_t i = 0; i < kCourseMapCount; ++i) {
    course_images[i] = lv_image_create(maps[i]);
    lv_obj_set_pos(course_images[i], 0, 0);
    lv_obj_set_size(course_images[i], kCourseMapSize, kCourseMapSize);
    lv_image_set_src(course_images[i], courseAsset(displayed_course_index).image);
    lv_obj_remove_flag(course_images[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(course_images[i]);
  }
  createPlanMap(plan_maps[0], objects.course_container);
  createPlanMap(plan_maps[1], objects.waiting_course_container);
  lv_obj_t *position_backings[kCourseMapCount] = {
      objects.position_marker_backing,            objects.waiting_position_marker_backing,
      objects.finished_position_marker_backing,   objects.gpsstale_position_marker_backing,
      nullptr,                                    objects.overtime_position_marker_backing,
      objects.plandemo_position_marker_backing,   objects.cachedplan_position_marker_backing,
      objects.expiredplan_position_marker_backing, objects.lapcorrected_position_marker_backing};
  lv_obj_t *position_arrows[kCourseMapCount] = {
      objects.position_marker,            objects.waiting_position_marker,
      objects.finished_position_marker,   objects.gpsstale_position_marker,
      nullptr,                            objects.overtime_position_marker,
      objects.plandemo_position_marker,   objects.cachedplan_position_marker,
      objects.expiredplan_position_marker, objects.lapcorrected_position_marker};
  for (size_t i = 0; i < kCourseMapCount; ++i) {
    course_markers[i].lap_line = lv_line_create(maps[i]);
    lv_obj_set_pos(course_markers[i].lap_line, 0, 0);
    lv_obj_set_size(course_markers[i].lap_line, kCourseMapSize, kCourseMapSize);
    lv_obj_remove_flag(course_markers[i].lap_line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_color(course_markers[i].lap_line, lv_color_hex(kLapLineColor), 0);
    lv_obj_set_style_line_width(course_markers[i].lap_line, 6, 0);
    lv_obj_set_style_line_rounded(course_markers[i].lap_line, true, 0);
    visible(course_markers[i].lap_line, false);
    course_markers[i].start = createCourseMarker(maps[i], "START", 0x087F8C, 10);
    course_markers[i].goal = createCourseMarker(maps[i], "GOAL", 0xB43832, 120);
    course_markers[i].lap_legend =
        createCourseLegend(maps[i], "LAP", kLapLineColor, 230);
    // The live GPS arrow remains above a start/goal point when they coincide.
    if (position_backings[i]) lv_obj_move_foreground(position_backings[i]);
    if (position_arrows[i]) lv_obj_move_foreground(position_arrows[i]);
  }
  setupSelectionUi();
  course_markers_positioned = false;
  loadScreen(SCREEN_ID_WAITING);
}
void viewUpdate() {
  vega::Snapshot s;
  if (!snapshot(s)) return;
  if (s.course_index != displayed_course_index) {
    displayed_course_index = s.course_index;
    for (auto *image : course_images)
      lv_image_set_src(image, courseAsset(displayed_course_index).image);
    course_markers_positioned = false;
    for (auto &map : plan_maps) hidePlanMap(map);
    if (selection_list) lv_obj_clean(selection_list);
    choices_initialized = false;
    text(selection_message, "LOADING STRATEGIES...");
  }
  lv_obj_t *extra_lap_fields[] = {
      objects.settings_lap5_title, objects.settings_lap5_button,
      objects.settings_lap6_title, objects.settings_lap6_button,
      objects.settings_lap7_title, objects.settings_lap7_button};
  for (size_t i = 0; i < 3; ++i) {
    visible(extra_lap_fields[i * 2], s.lap_count > i + 4);
    visible(extra_lap_fields[i * 2 + 1], s.lap_count > i + 4);
  }
  for (size_t i = 0; i < courseCount() && i < 8; ++i)
    if (course_buttons[i]) {
      const auto color = lv_color_hex(i == s.course_index ? 0x087F8C : 0x1769B2);
      if (!lv_color_eq(lv_obj_get_style_bg_color(course_buttons[i], LV_PART_MAIN), color))
        lv_obj_set_style_bg_color(course_buttons[i], color, 0);
      enabled(course_buttons[i], s.engine == vega::EnginePhase::Off);
    }
  enabled(selection_refresh, s.engine == vega::EnginePhase::Off);
  if (selection_list && choices_initialized)
    for (uint8_t i = 0; i < shown_choices.count; ++i)
      enabled(lv_obj_get_child(selection_list, i),
              shown_choices.items[i].valid && s.engine == vega::EnginePhase::Off);
  presenter.setLapCount(s.lap_count);
  if (finish_pending && s.race.phase == vega::RacePhase::Measuring &&
      s.now_ms - finish_requested_ms > 2000)
    finish_pending = false;
  if (save_pending) {
    bool same = sameSettings(s.settings, pending_settings);
    if (s.settings_attempt != pending_settings_attempt || s.now_ms - save_started > 3000) {
      save_pending = false;
      setMessage(s.settings_attempt != pending_settings_attempt && s.settings_accepted && same
                     ? "SETTINGS SAVED"
                 : s.settings_error ? "SAVE FAILED - SETTINGS NOT CHANGED"
                                    : "SAVE NOT ACCEPTED - TRY AGAIN");
    }
  }
  vega::Strategy strategy_data;
  const bool plan_loaded = strategy(strategy_data);
  const auto ui_status = status();
  presenter.render(s, ui_status, plan_loaded ? &strategy_data : nullptr);
  static PlanChoices choices{};
  if (planChoices(choices) && choices.course_index == s.course_index) {
    refreshSelectionList(choices);
    if (s.engine != vega::EnginePhase::Off)
      text(selection_message, "ELECTRICAL OFF TO CHANGE SELECTION");
    text(objects.waiting_plan_status_label, courseAsset(s.course_index).short_name);
    char plan_caption[72];
    if (ui_status.plan_state == vega::PlanState::Loading)
      snprintf(plan_caption, sizeof(plan_caption), "PLAN LOADING");
    else if (choices.selected && choices.selected < choices.count)
      snprintf(plan_caption, sizeof(plan_caption), "PLAN %s", choices.items[choices.selected].label);
    else if (!choices.sd_available)
      snprintf(plan_caption, sizeof(plan_caption), "SD NOT AVAILABLE");
    else if (ui_status.plan_state == vega::PlanState::Invalid)
      snprintf(plan_caption, sizeof(plan_caption), "PLAN INVALID");
    else if (choices.selected == 255)
      snprintf(plan_caption, sizeof(plan_caption), "PLAN MISSING");
    else snprintf(plan_caption, sizeof(plan_caption), "PLAN NONE");
    text(objects.waiting_race_status_label, plan_caption);
  } else {
    text(objects.waiting_plan_status_label, courseAsset(s.course_index).short_name);
    text(objects.waiting_race_status_label, "PLAN LOADING");
  }
  updateCourseMarkers(s.settings);
  char start[90];
  if (s.gps_fresh) {
    const auto &p = s.settings.start;
    double north = (s.gps.position.latitude - p.latitude) * courseData().north_per_degree;
    double east = (s.gps.position.longitude - p.longitude) * courseData().east_per_degree;
    std::snprintf(start, sizeof(start), "START POSITION: %.0f m", std::hypot(north, east));
  } else
    std::snprintf(start, sizeof(start), "START POSITION: GPS UNAVAILABLE");
  text(objects.waiting_start_position_label, start);
}
void viewOpenSettings() {
  draft = presenter.settings();
  save_pending = false;
  refreshFields();
  refreshGpsSourceSelector();
  refreshBrightnessControl();
  setMessage(presenter.phase() == vega::RacePhase::Measuring
                 ? "TIMING ACTIVE - SETTINGS LOCKED"
                 : "TARGET / MM:SS - COORDINATES / DEGREES");
  visible(objects.settings_advanced_overlay, false);
  visible(objects.settings_general_overlay, false);
  visible(objects.settings_editor_overlay, false);
  visible(objects.cancel_confirmation_overlay, false);
}
void viewReturnDashboard() {
  visible(objects.settings_advanced_overlay, false);
  visible(objects.settings_general_overlay, false);
  visible(objects.settings_editor_overlay, false);
  visible(objects.cancel_confirmation_overlay, false);
  loadScreen(live_screen);
}
void viewOpenAdvanced() {
  text(objects.settings_advanced_message_label,
       presenter.phase() == vega::RacePhase::Measuring
           ? "TIMING ACTIVE - SETTINGS LOCKED" : "COURSE GEOMETRY AND LAP RULES");
  visible(objects.settings_general_overlay, false);
  visible(objects.settings_advanced_overlay, true);
  lv_obj_move_foreground(objects.settings_advanced_overlay);
}
void viewCloseAdvanced() { visible(objects.settings_advanced_overlay, false); }
void viewOpenGeneral() {
  text(objects.settings_general_message_label,
       presenter.phase() == vega::RacePhase::Measuring
           ? "TIMING ACTIVE - SETTINGS LOCKED" : "DEVICE DISPLAY, GPS AND VEHICLE INPUTS");
  visible(objects.settings_advanced_overlay, false);
  visible(objects.settings_general_overlay, true);
  lv_obj_move_foreground(objects.settings_general_overlay);
}
void viewCloseGeneral() { viewReturnDashboard(); }

void selectGpsSource(vega::GpsSource source) {
  if (presenter.phase() == vega::RacePhase::Measuring || save_pending) return;
  draft.gps_source = source;
  refreshGpsSourceSelector();
  setMessage(source == vega::GpsSource::PortA ? "PORT.A SELECTED - SAVE SETTINGS"
                                           : "M5BUS SELECTED - SAVE SETTINGS");
}

void editField(size_t index) {
  if (index >= vega::kSettingsFieldCount || presenter.phase() == vega::RacePhase::Measuring ||
      save_pending)
    return;
  editing_field = index;
  char value[32];
  vega::settingText(draft, index, value, sizeof(value));
  text(objects.settings_editor_title, vega::settingTitle(index));
  // set_text filters each character using the current accepted_chars.
  lv_textarea_set_accepted_chars(objects.settings_editor_input,
                                 index < 8                          ? "0123456789:"
                                 : index < 14                       ? "0123456789.-"
                                 : index == 14 || (index >= 22 && index <= 24)
                                                                     ? "0123456789."
                                                                     : "0123456789");
  lv_textarea_set_text(objects.settings_editor_input, value);
  lv_textarea_set_cursor_pos(objects.settings_editor_input, LV_TEXTAREA_CURSOR_LAST);
  visible(objects.settings_editor_overlay, true);
  lv_obj_move_foreground(objects.settings_editor_overlay);
}
void pressKey(size_t index) {
  if (index >= sizeof(kKeypadKeys) / sizeof(kKeypadKeys[0]) ||
      lv_obj_has_flag(objects.settings_editor_overlay, LV_OBJ_FLAG_HIDDEN))
    return;
  const char *key = kKeypadKeys[index];
  if (!strcmp(key, "DEL"))
    lv_textarea_delete_char(objects.settings_editor_input);
  else if (!strcmp(key, "CLR"))
    lv_textarea_set_text(objects.settings_editor_input, "");
  else if (!strcmp(key, "SET"))
    finishEdit();
  else
    lv_textarea_add_text(objects.settings_editor_input, key);
}
void finishEdit() {
  if (presenter.phase() == vega::RacePhase::Measuring) return;
  if (!vega::editSetting(draft, editing_field,
                         lv_textarea_get_text(objects.settings_editor_input))) {
    text(objects.settings_editor_title,
         editing_field < 8 ? "INVALID - USE MM:SS"
         : editing_field < 14 ? "INVALID COORDINATE" : "INVALID VALUE OR RANGE");
    return;
  }
  refreshFields();
  visible(objects.settings_editor_overlay, false);
}
void discardEdit() { visible(objects.settings_editor_overlay, false); }
void saveSettings() {
  vega::Snapshot before;
  if (!snapshot(before)) return;
  const auto scope = !lv_obj_has_flag(objects.settings_general_overlay, LV_OBJ_FLAG_HIDDEN)
                         ? vega::SettingsScope::General : vega::SettingsScope::Course;
  Command c{};
  c.kind = CommandKind::Configure;
  c.scope = scope;
  c.settings = before.settings;
  if (scope == vega::SettingsScope::General)
    vega::applyGeneral(c.settings, vega::generalSettings(draft));
  else
    vega::applyCourse(c.settings, vega::courseSettings(draft));
  if (!presenter.request(c)) {
    setMessage("SETTINGS NOT ACCEPTED");
    return;
  }
  pending_settings = c.settings;
  save_pending = true;
  save_started = before.now_ms;
  pending_settings_attempt = before.settings_attempt;
  setMessage("SAVING SETTINGS...");
}
void confirmCancel() {
  if (presenter.phase() != vega::RacePhase::Measuring) return;
  action_open_settings(nullptr);
  visible(objects.cancel_confirmation_overlay, true);
  lv_obj_move_foreground(objects.cancel_confirmation_overlay);
}
void cancelTiming() {
  if (request(CommandKind::Cancel)) {
    visible(objects.cancel_confirmation_overlay, false);
    viewReturnDashboard();
  }
}
void dismissCancel() { visible(objects.cancel_confirmation_overlay, false); }
void startTiming() {
  if (request(CommandKind::Start)) enabled(objects.start_button, false);
}
void manualLap() {
  if (presenter.phase() != vega::RacePhase::Measuring || !finish_ready) return;
  if (finish_mode) {
    visible(finish_overlay, true);
    lv_obj_move_foreground(finish_overlay);
  } else if (request(CommandKind::Lap)) {
    enabled(objects.manual_lap_button, false);
  }
}
void manualFinishConfirm() {
  if (!finish_mode || !finish_ready || finish_pending ||
      presenter.phase() != vega::RacePhase::Measuring)
    return;
  if (request(CommandKind::Finish)) {
    vega::Snapshot s;
    if (snapshot(s)) finish_requested_ms = s.now_ms;
    finish_pending = true;
    visible(finish_overlay, false);
  }
}
void manualFinishDismiss() { visible(finish_overlay, false); }
void ignite() {
  if (request(CommandKind::Ignite)) {
    ignition_pending = true;
    enabled(objects.ignition_switch, false);
    enabled(objects.waiting_ignition_switch, false);
    enabled(objects.finished_ignition_switch, false);
  }
}
lv_obj_t *controlHit(int16_t x, int16_t y) {
  auto *screen = lv_screen_active();
  const ControlPage *page = nullptr;
  for (const auto &entry : control_pages)
    if (entry.screen == screen) page = &entry;
  if (!page) return nullptr;

  lv_point_t point{x, y};
  auto *display = lv_display_get_default();
  // System/top layers and EEZ overlays take precedence over dashboard controls.
  if (finish_overlay && !lv_obj_has_flag(finish_overlay, LV_OBJ_FLAG_HIDDEN)) {
    auto *hit = lv_indev_search_obj(screen, &point);
    if (hit == page->power && !lv_obj_has_state(hit, LV_STATE_DISABLED)) return hit;
    return nullptr;
  }
  if (lv_indev_search_obj(lv_display_get_layer_sys(display), &point) ||
      lv_indev_search_obj(lv_display_get_layer_top(display), &point))
    return nullptr;
  auto *hit = lv_indev_search_obj(screen, &point);
  if (hit == page->power && !lv_obj_has_state(hit, LV_STATE_DISABLED)) return hit;
  if (hit == page->ignition && !lv_obj_has_state(hit, LV_STATE_DISABLED)) return hit;
  return nullptr;
}
void controlAction(lv_obj_t *control) {
  for (const auto &page : control_pages) {
    if (lv_screen_active() != page.screen) continue;
    if (control == page.power) {
      const auto next = power_intent.nextTap();
      if (next == PowerRequest::None) return;
      if (request(next == PowerRequest::On ? CommandKind::PowerOn : CommandKind::PowerOff)) {
        ++power_taps;
        power_intent.accepted(next);
      }
      return;
    }
    if (control == page.ignition && !lv_obj_has_state(control, LV_STATE_DISABLED)) {
      ++ignition_taps;
      ignite();
      return;
    }
  }
}
void controlCleanup() {
  for (const auto &page : control_pages) {
    for (auto *control : {page.power, page.ignition}) {
      if (lv_obj_has_state(control, LV_STATE_PRESSED)) {
        lv_obj_remove_state(control, LV_STATE_PRESSED);
        ++stale_control_presses;
      }
    }
  }
}
bool viewDiagnostic(const char *command) {
  if (!objects.waiting || strncmp(command, "ui-", 3)) return false;
  if (!strcmp(command, "ui-status")) {
    Serial.printf(
        "[UI] screen=%s start_enabled=%u ignition_enabled=%u settings_enabled=%u "
        "advanced_visible=%u general_visible=%u editor_visible=%u "
        "message=%s\n",
        lv_screen_active() == objects.settings  ? "settings"
        : lv_screen_active() == objects.waiting ? "waiting"
        : lv_screen_active() == objects.main    ? "main"
                                                : "other",
        !lv_obj_has_state(objects.start_button, LV_STATE_DISABLED),
        !lv_obj_has_state(objects.waiting_ignition_switch, LV_STATE_DISABLED),
        !lv_obj_has_state(objects.settings_save_button, LV_STATE_DISABLED),
        !lv_obj_has_flag(objects.settings_advanced_overlay, LV_OBJ_FLAG_HIDDEN),
        !lv_obj_has_flag(objects.settings_general_overlay, LV_OBJ_FLAG_HIDDEN),
        !lv_obj_has_flag(objects.settings_editor_overlay, LV_OBJ_FLAG_HIDDEN), message);
  } else if (!strcmp(command, "ui-touch")) {
    auto *screen = lv_screen_active();
    const ControlPage *active = nullptr;
    for (const auto &page : control_pages)
      if (screen == page.screen) active = &page;
    tab5_lvgl_report_touch_state();
    Serial.printf("[UI TOUCH] screen=%s power_pressed=%u power_checked=%u "
                  "ignition_pressed=%u ignition_disabled=%u power_taps=%lu ignition_taps=%lu "
                  "stale_cleared=%lu\n",
                  active ? active->name : "other",
                  active && lv_obj_has_state(active->power, LV_STATE_PRESSED),
                  active && lv_obj_has_state(active->power, LV_STATE_CHECKED),
                  active && lv_obj_has_state(active->ignition, LV_STATE_PRESSED),
                  active && lv_obj_has_state(active->ignition, LV_STATE_DISABLED),
                  static_cast<unsigned long>(power_taps),
                  static_cast<unsigned long>(ignition_taps),
                  static_cast<unsigned long>(stale_control_presses));
  } else if (!strcmp(command, "ui-course-markers")) {
    size_t page_index = 0;
    for (size_t i = 0; i < control_pages.size(); ++i)
      if (control_pages[i].screen == lv_screen_active()) page_index = i;
    const auto &markers = course_markers[page_index];
    Serial.printf("[UI MAP] screen=%s start=%d,%d visible=%u goal=%d,%d visible=%u "
                  "lap=%ld,%ld-%ld,%ld visible=%u\n",
                  control_pages[page_index].name,
                  lv_obj_get_x(markers.start.point) + 9, lv_obj_get_y(markers.start.point) + 9,
                  !lv_obj_has_flag(markers.start.point, LV_OBJ_FLAG_HIDDEN),
                  lv_obj_get_x(markers.goal.point) + 9, lv_obj_get_y(markers.goal.point) + 9,
                  !lv_obj_has_flag(markers.goal.point, LV_OBJ_FLAG_HIDDEN),
                  static_cast<long>(std::lround(markers.lap_points[0].x)),
                  static_cast<long>(std::lround(markers.lap_points[0].y)),
                  static_cast<long>(std::lround(markers.lap_points[1].x)),
                  static_cast<long>(std::lround(markers.lap_points[1].y)),
                  !lv_obj_has_flag(markers.lap_line, LV_OBJ_FLAG_HIDDEN));
  } else if (!strcmp(command, "ui-plan")) {
    const auto &plan_map =
        lv_screen_active() == objects.waiting ? plan_maps[1] : plan_maps[0];
    Serial.printf("[UI PLAN] shown=%u lap=%u orange=%u blue=%u on=%d,%d off=%d,%d "
                  "action=%s plan_status=%s\n",
                  plan_map.shown, plan_map.shown_lap,
                  plan_map.shown &&
                      !lv_obj_has_flag(plan_map.acceleration[0].object, LV_OBJ_FLAG_HIDDEN),
                  plan_map.shown &&
                      !lv_obj_has_flag(plan_map.coasting[0].object, LV_OBJ_FLAG_HIDDEN),
                  lv_obj_get_x(plan_map.on_flame[0]) + 20,
                  lv_obj_get_y(plan_map.on_flame[0]) + 20,
                  lv_obj_get_x(plan_map.off_square[0]) + 12,
                  lv_obj_get_y(plan_map.off_square[0]) + 12,
                  lv_label_get_text(objects.next_action_label),
                  lv_label_get_text(objects.plan_status_label));
  } else if (!strcmp(command, "ui-settings"))
    action_open_settings(nullptr);
  else if (!strcmp(command, "ui-general-menu"))
    action_open_general_menu(nullptr);
  else if (!strcmp(command, "ui-advanced"))
    action_open_advanced_settings(nullptr);
  else if (!strcmp(command, "ui-general"))
    viewOpenGeneral();
  else if (!strcmp(command, "ui-advanced-back"))
    action_close_advanced_settings(nullptr);
  else if (!strcmp(command, "ui-general-back"))
    action_close_general_settings(nullptr);
  else if (!strcmp(command, "ui-back"))
    viewReturnDashboard();
  else if (!strcmp(command, "ui-start"))
    lv_obj_send_event(objects.start_button, LV_EVENT_CLICKED, nullptr);
  else if (!strcmp(command, "ui-save"))
    lv_obj_send_event(objects.settings_save_button, LV_EVENT_CLICKED, nullptr);
  else if (!strcmp(command, "ui-cancel"))
    action_confirm_cancel(nullptr);
  else if (!strcmp(command, "ui-confirm-cancel"))
    lv_obj_send_event(objects.cancel_confirmation_yes, LV_EVENT_CLICKED, nullptr);
  else if (!strncmp(command, "ui-inspect-field ", 17)) {
    unsigned index;
    char extra;
    if (sscanf(command + 17, "%u%c", &index, &extra) == 1 &&
        index < vega::kSettingsFieldCount) {
      if ((index >= 14 && index <= 21) || index == 23) viewOpenGeneral();
      else if (index >= 22) viewOpenAdvanced();
      lv_obj_send_event(fieldButton(index), LV_EVENT_CLICKED, nullptr);
      if (!lv_obj_has_flag(objects.settings_editor_overlay, LV_OBJ_FLAG_HIDDEN)) {
        Serial.printf("[UI FIELD] index=%u text=%s\n", index,
                      lv_textarea_get_text(objects.settings_editor_input));
        discardEdit();
      } else
        Serial.printf("[UI FIELD] index=%u locked\n", index);
    }
  }
  else if (!strncmp(command, "ui-edit ", 8)) {
    unsigned index;
    char value[25];
    if (sscanf(command + 8, "%u %24s", &index, value) == 2 && index < vega::kSettingsFieldCount) {
      if ((index >= 14 && index <= 21) || index == 23) viewOpenGeneral();
      else if (index >= 22) viewOpenAdvanced();
      lv_obj_send_event(fieldButton(index), LV_EVENT_CLICKED, nullptr);
      if (!lv_obj_has_flag(objects.settings_editor_overlay, LV_OBJ_FLAG_HIDDEN)) {
        lv_textarea_set_text(objects.settings_editor_input, value);
        lv_obj_send_event(objects.settings_editor_done, LV_EVENT_CLICKED, nullptr);
      }
    }
  } else
    Serial.println("[UI] unknown diagnostic command");
  return true;
}
}  // namespace tab5

extern "C" void action_start_timing(lv_event_t *) { tab5::startTiming(); }
extern "C" void action_manual_lap(lv_event_t *) { tab5::manualLap(); }
extern "C" void action_electrical_changed(lv_event_t *) {}
extern "C" void action_ignite(lv_event_t *) {}
extern "C" void action_edit_setting(lv_event_t *e) {
  tab5::editField(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
}
extern "C" void action_finish_edit(lv_event_t *) { tab5::finishEdit(); }
extern "C" void action_discard_edit(lv_event_t *) { tab5::discardEdit(); }
extern "C" void action_save_settings(lv_event_t *) { tab5::saveSettings(); }
extern "C" void action_select_gps_m5bus(lv_event_t *) {
  tab5::selectGpsSource(vega::GpsSource::M5Bus);
}
extern "C" void action_select_gps_port_a(lv_event_t *) {
  tab5::selectGpsSource(vega::GpsSource::PortA);
}
extern "C" void action_confirm_cancel(lv_event_t *) { tab5::confirmCancel(); }
extern "C" void action_cancel_timing(lv_event_t *) { tab5::cancelTiming(); }
extern "C" void action_dismiss_cancel(lv_event_t *) { tab5::dismissCancel(); }
