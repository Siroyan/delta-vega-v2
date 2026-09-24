#include "tab5_lvgl.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

namespace {

constexpr size_t kDrawBufferLines = 72;  // One tenth of Tab5's 720-pixel height.

uint32_t tick_ms() { return millis(); }

void flush_display(lv_display_t *display, const lv_area_t *area,
                   uint8_t *pixel_map) {
  const int32_t width = area->x2 - area->x1 + 1;
  const int32_t height = area->y2 - area->y1 + 1;

  // Explicit RGB565 type avoids M5GFX's configurable byte-swap overload.
  M5.Display.pushImage(area->x1, area->y1, width, height,
                       reinterpret_cast<const lgfx::rgb565_t *>(pixel_map));
  lv_display_flush_ready(display);
}

void read_touch(lv_indev_t *, lv_indev_data_t *data) {
  static bool was_pressed = false;
  if (M5.Touch.getCount() == 0 || !M5.Touch.getDetail().isPressed()) {
    if (was_pressed) {
      Serial.println("Touch released");
    }
    was_pressed = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  const auto touch = M5.Touch.getDetail();
  if (!was_pressed) {
    Serial.printf("Touch pressed: %d, %d\n", touch.x, touch.y);
  }
  was_pressed = true;
  data->point.x = touch.x;
  data->point.y = touch.y;
  data->state = LV_INDEV_STATE_PRESSED;
}

}  // namespace

bool tab5_lvgl_begin() {
  const int32_t width = M5.Display.width();
  const int32_t height = M5.Display.height();
  if (width <= 0 || height <= 0) {
    Serial.println("LVGL: display initialization failed");
    return false;
  }

  lv_init();
  lv_tick_set_cb(tick_ms);

  const size_t lines = height < kDrawBufferLines ? height : kDrawBufferLines;
  const size_t buffer_bytes = static_cast<size_t>(width) * lines * sizeof(uint16_t);
  void *draw_buffer =
      heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (draw_buffer == nullptr) {
    Serial.println("LVGL: PSRAM draw buffer allocation failed");
    return false;
  }

  lv_display_t *display = lv_display_create(width, height);
  if (display == nullptr) {
    heap_caps_free(draw_buffer);
    Serial.println("LVGL: display registration failed");
    return false;
  }
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, flush_display);
  lv_display_set_buffers(display, draw_buffer, nullptr, buffer_bytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_display(touch, display);
  lv_indev_set_read_cb(touch, read_touch);

  Serial.printf("LVGL: %ld x %ld, draw buffer %u bytes\n",
                static_cast<long>(width), static_cast<long>(height),
                static_cast<unsigned>(buffer_bytes));
  return true;
}

void tab5_lvgl_update() {
  M5.update();
  lv_timer_handler();
}
