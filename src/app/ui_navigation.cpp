#include "../adapters/lvgl_view.h"
#include "../ui/actions.h"
#include "../ui/screens.h"
#include "../ui/ui.h"

namespace {

ScreensEnum dashboard_screen = SCREEN_ID_MAIN;

struct DashboardEntry {
  ScreensEnum id;
  lv_obj_t **screen;
  lv_obj_t **overlay;
};

const DashboardEntry dashboards[] = {
    {SCREEN_ID_MAIN, &objects.main, &objects.navigation_overlay},
    {SCREEN_ID_WAITING, &objects.waiting, &objects.waiting_navigation_overlay},
    {SCREEN_ID_FINISHED, &objects.finished, &objects.finished_navigation_overlay},
    {SCREEN_ID_GPS_STALE, &objects.gps_stale, &objects.gpsstale_navigation_overlay},
    {SCREEN_ID_MISSING_DATA, &objects.missing_data, &objects.missingdata_navigation_overlay},
    {SCREEN_ID_OVERTIME, &objects.overtime, &objects.overtime_navigation_overlay},
    {SCREEN_ID_PLAN_DEMO, &objects.plan_demo, &objects.plandemo_navigation_overlay},
    {SCREEN_ID_CACHED_PLAN, &objects.cached_plan, &objects.cachedplan_navigation_overlay},
    {SCREEN_ID_EXPIRED_PLAN, &objects.expired_plan, &objects.expiredplan_navigation_overlay},
    {SCREEN_ID_LAP_CORRECTED, &objects.lap_corrected, &objects.lapcorrected_navigation_overlay},
};

void close_menus() {
  for (const auto &entry : dashboards) {
    if (*entry.overlay != nullptr) {
      lv_obj_add_flag(*entry.overlay, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

}  // namespace

extern "C" void action_open_menu(lv_event_t *event) {
  auto *screen = lv_obj_get_screen(lv_event_get_target_obj(event));
  for (const auto &entry : dashboards) {
    if (*entry.screen == screen && *entry.overlay != nullptr) {
      close_menus();
      dashboard_screen = entry.id;
      lv_obj_remove_flag(*entry.overlay, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(*entry.overlay);
      return;
    }
  }
}

extern "C" void action_close_menu(lv_event_t *) { close_menus(); }

extern "C" void action_open_settings(lv_event_t *) {
  close_menus();
  tab5::viewOpenSettings();
  loadScreen(SCREEN_ID_SETTINGS);
}

extern "C" void action_open_advanced_settings(lv_event_t *) { tab5::viewOpenAdvanced(); }

extern "C" void action_close_advanced_settings(lv_event_t *) { tab5::viewCloseAdvanced(); }

extern "C" void action_return_to_dashboard(lv_event_t *) {
  close_menus();
  tab5::viewReturnDashboard();
}
