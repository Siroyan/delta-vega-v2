#ifndef LV_CONF_H
#define LV_CONF_H

// Tab5 display and LVGL draw buffer both use RGB565.
#define LV_COLOR_DEPTH 16

// Keep LVGL objects out of the internal DMA heap used by ESP-Hosted Wi-Fi.
// The display draw buffer is allocated separately in PSRAM.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (1024U * 1024U)
#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_48

#endif  // LV_CONF_H
