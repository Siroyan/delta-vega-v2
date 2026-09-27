#pragma once

// Call after M5.begin(). Returns false if the display or draw buffer is unavailable.
bool tab5_lvgl_begin();

// Call regularly from the Arduino loop before ui_tick().
void tab5_lvgl_update();
