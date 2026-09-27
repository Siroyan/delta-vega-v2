#ifndef EEZ_LVGL_UI_IMAGES_H
#define EEZ_LVGL_UI_IMAGES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t img_motegi_background_480;
extern const lv_img_dsc_t img_electrical_power_off;
extern const lv_img_dsc_t img_electrical_power_on;
extern const lv_img_dsc_t img_ignition_flame;
extern const lv_img_dsc_t img_menu_dark;
extern const lv_img_dsc_t img_x_dark;
extern const lv_img_dsc_t img_arrow_left_dark;
extern const lv_img_dsc_t img_settings_dark;
extern const lv_img_dsc_t img_course_ignition_flame;

#ifndef EXT_IMG_DESC_T
#define EXT_IMG_DESC_T
typedef struct _ext_img_desc_t {
    const char *name;
    const lv_img_dsc_t *img_dsc;
} ext_img_desc_t;
#endif

extern const ext_img_desc_t images[9];

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_IMAGES_H*/