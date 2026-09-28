#pragma once

#include <stdint.h>

// Call after M5.begin(). Returns false if the display or draw buffer is unavailable.
bool tab5_lvgl_begin();

// Call regularly from the Arduino loop before ui_tick().
void tab5_lvgl_update();

// Record main-loop timings and print touch/render latency since the previous report.
void tab5_lvgl_note_loop(uint32_t loop_gap_us, uint32_t serial_us, uint32_t view_us,
                        uint32_t ui_tick_us);
void tab5_lvgl_report_perf();
