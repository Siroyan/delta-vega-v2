#pragma once

#include <stdint.h>
#include <lvgl.h>

using ControlHitTest = lv_obj_t *(*)(int16_t x, int16_t y);
enum class ControlEvent : uint8_t { Press, Release, Tap };
using ControlAction = void (*)(lv_obj_t *control, ControlEvent event);
using ControlCleanup = void (*)();
using ControlReleaseFallback = void (*)(uint64_t released_us);
void tab5_lvgl_set_control_handlers(ControlHitTest hit_test, ControlAction action,
                                   ControlCleanup cleanup, ControlReleaseFallback release);
// After an ignition press is accepted, the touch sampler signals release even
// when LVGL is busy. Both paths call the release callback at most once.
void tab5_lvgl_arm_control_release();
void tab5_lvgl_finish_control_release();

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
