#include <M5Unified.h>
#include <esp_timer.h>
#include <lvgl.h>

#include "adapters/lvgl_view.h"
#include "adapters/tab5_audio.h"
#include "adapters/tab5_runtime.h"
#include "tab5_lvgl.h"
#include "ui/ui.h"

void setup() {
  // Apply the configured OFF polarity before initializing the display.
  bool outputs_ready = tab5::prepareOutputs();
  auto config = M5.config();
  config.serial_baudrate = 115200;
  M5.begin(config);
  if (!tab5::audioBegin()) Serial.println("[AUDIO] initialization failed");
  M5.Display.setRotation(1);
  Serial.println("[BOOT] Delta Vega v2 / Ports and Adapters / MVP");
  if (!outputs_ready || !tab5::begin()) {
    Serial.println("[FATAL] application initialization failed; electrical OFF");
    tab5::outputsOff();
    for (;;) delay(1000);
  }
  if (!tab5_lvgl_begin()) {
    Serial.println("[FATAL] LVGL initialization failed; application keeps running");
    return;
  }
  ui_init();
  tab5::viewBegin();
  Serial.println("[BOOT] UI ready; no simulated sensor data");
}

void loop() {
  int64_t stage = esp_timer_get_time();
  static int64_t previous_loop = stage;
  uint32_t loop_gap_us = stage - previous_loop;
  previous_loop = stage;
  tab5::serialPoll();
  uint32_t serial_us = esp_timer_get_time() - stage;
  uint32_t view_us = 0, ui_tick_us = 0;
  if (objects.waiting) {
    static uint32_t last_view_ms = 0;
    uint32_t now = millis();
    if (now - last_view_ms >= 100) {
      last_view_ms = now;
      stage = esp_timer_get_time();
      tab5::viewUpdate();
      view_us = esp_timer_get_time() - stage;
    }
    tab5_lvgl_update();
    stage = esp_timer_get_time();
    ui_tick();
    ui_tick_us = esp_timer_get_time() - stage;
  }
  tab5_lvgl_note_loop(loop_gap_us, serial_us, view_us, ui_tick_us);
  delay(5);
}
