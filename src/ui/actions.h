#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_open_menu(lv_event_t * e);
extern void action_close_menu(lv_event_t * e);
extern void action_open_settings(lv_event_t * e);
extern void action_return_to_dashboard(lv_event_t * e);
extern void action_start_timing(lv_event_t * e);
extern void action_manual_lap(lv_event_t * e);
extern void action_electrical_changed(lv_event_t * e);
extern void action_ignite(lv_event_t * e);
extern void action_edit_setting(lv_event_t * e);
extern void action_finish_edit(lv_event_t * e);
extern void action_discard_edit(lv_event_t * e);
extern void action_save_settings(lv_event_t * e);
extern void action_confirm_cancel(lv_event_t * e);
extern void action_cancel_timing(lv_event_t * e);
extern void action_dismiss_cancel(lv_event_t * e);
extern void action_open_advanced_settings(lv_event_t * e);
extern void action_close_advanced_settings(lv_event_t * e);
extern void action_select_gps_m5bus(lv_event_t * e);
extern void action_select_gps_port_a(lv_event_t * e);
extern void action_open_general_settings(lv_event_t * e);
extern void action_close_general_settings(lv_event_t * e);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/