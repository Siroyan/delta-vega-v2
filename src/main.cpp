#include <M5Unified.h>
#include <lvgl.h>

#include "adapters/lvgl_view.h"
#include "adapters/tab5_runtime.h"
#include "tab5_lvgl.h"
#include "ui/ui.h"

void setup() {
  // Apply the configured OFF polarity before initializing the display.
  bool outputs_ready = tab5::prepareOutputs();
  auto config = M5.config();
  config.serial_baudrate = 115200;
  M5.begin(config);
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
  tab5::serialPoll();
  if (objects.waiting) {
    static uint32_t last_view_ms = 0;
    uint32_t now = millis();
    if (now - last_view_ms >= 100) {
      last_view_ms = now;
      tab5::viewUpdate();
    }
    tab5_lvgl_update();
    ui_tick();
  }
  delay(5);
}
