#pragma once

#include <stdint.h>
#include <lvgl.h>

using ControlHitTest = lv_obj_t *(*)(int16_t x, int16_t y);
using ControlAction = void (*)(lv_obj_t *control);
using ControlCleanup = void (*)();
void tab5_lvgl_set_control_handlers(ControlHitTest hit_test, ControlAction action,
                                   ControlCleanup cleanup);

// Call after M5.begin(). Returns false if the display or draw buffer is unavailable.
bool tab5_lvgl_begin();
void tab5_lvgl_set_brightness(uint8_t brightness);

// Call regularly from the Arduino loop before ui_tick().
void tab5_lvgl_update();

// Record main-loop timings and print touch/render latency since the previous report.
void tab5_lvgl_note_loop(uint32_t loop_gap_us, uint32_t serial_us, uint32_t view_us,
                        uint32_t ui_tick_us);
void tab5_lvgl_report_perf();
// Read-only snapshot for investigating a touch that remains visually pressed.
void tab5_lvgl_report_touch_state();
