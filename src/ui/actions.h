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

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/