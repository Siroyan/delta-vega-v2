#ifndef LV_CONF_H
#define LV_CONF_H

// Tab5 display and LVGL draw buffer both use RGB565.
#define LV_COLOR_DEPTH 16

// Let ESP-IDF's allocator manage LVGL objects instead of reserving a fixed
// internal-RAM pool. The draw buffer is allocated separately in PSRAM.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB

#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_48

#endif  // LV_CONF_H
