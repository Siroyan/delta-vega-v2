#pragma once

#include <stdint.h>
#include <lvgl.h>
#include "control_gesture.h"

using ControlHitTest = lv_obj_t *(*)(int16_t x, int16_t y);
enum class ControlEvent : uint8_t { Press, Release, Tap };
using ControlAction = void (*)(lv_obj_t *control, ControlEvent event, const ControlTouch &touch);
using ControlCleanup = void (*)();
using ControlReleaseFallback = void (*)(uint64_t released_us);
void tab5_lvgl_set_control_handlers(ControlHitTest hit_test, ControlAction action,
                                   ControlCleanup cleanup, ControlReleaseFallback release);
// Reserve release tracking before submitting IGNITE. Activate after acceptance,
// or cancel on rejection. Both release paths notify at most once, after activation.
bool tab5_lvgl_prepare_control_release(uint64_t gesture_us);
void tab5_lvgl_activate_control_release();
void tab5_lvgl_cancel_control_release();
void tab5_lvgl_finish_control_release(const ControlTouch &touch);

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
