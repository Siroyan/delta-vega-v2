#include <M5Unified.h>
#include <lvgl.h>

#include "tab5_lvgl.h"

#if __has_include("ui/ui.h")
#include "ui/ui.h"
#define HAS_EEZ_UI 1
#else
#define HAS_EEZ_UI 0
#endif

namespace {

#if !HAS_EEZ_UI
void on_button_clicked(lv_event_t *event) {
  auto *label = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
  lv_label_set_text(label, "Touch OK");
  Serial.println("Touch OK");
}

void create_demo_ui() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x102030), 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "Hello World");
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -70);

  lv_obj_t *button = lv_button_create(screen);
  lv_obj_set_size(button, 240, 80);
  lv_obj_align(button, LV_ALIGN_CENTER, 0, 60);
  lv_obj_add_event_cb(button, on_button_clicked, LV_EVENT_CLICKED, title);

  lv_obj_t *button_text = lv_label_create(button);
  lv_label_set_text(button_text, "Tap me");
  lv_obj_center(button_text);
}
#endif

}  // namespace

void setup() {
  auto config = M5.config();
  config.serial_baudrate = 115200;
  M5.begin(config);
  M5.Display.setRotation(1);  // Landscape: 1280 x 720.

  if (!tab5_lvgl_begin()) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.drawString("LVGL initialization failed", 20, 20);
    while (true) {
      delay(1000);
    }
  }

#if HAS_EEZ_UI
  ui_init();
#else
  create_demo_ui();
#endif
  Serial.println("Hello World");
}

void loop() {
  tab5_lvgl_update();
#if HAS_EEZ_UI
  ui_tick();
#endif
  delay(5);
}
