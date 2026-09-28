#include "tab5_lvgl.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <lvgl.h>

#include <algorithm>
#include <atomic>

#include "control_gesture.h"

namespace {

constexpr size_t kDrawBufferLines = 72;  // One tenth of Tab5's 720-pixel height.
uint32_t max_lvgl_us = 0, max_flush_us = 0, max_input_lag_us = 0;
uint32_t max_loop_gap_us = 0, max_serial_us = 0, max_view_us = 0, max_ui_tick_us = 0;
uint32_t flushes = 0;
struct TouchSample {
  int16_t x = 0, y = 0;
  bool pressed = false;
  int64_t sampled_us = 0;
};
QueueHandle_t touch_events = nullptr;
lv_indev_t *touch_indev = nullptr;
TouchSample latest_touch;
portMUX_TYPE touch_lock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> max_touch_poll_us{0}, max_touch_gap_us{0}, touch_overflows{0};
std::atomic<uint32_t> touch_presses{0}, touch_releases{0};
std::atomic<uint32_t> touch_delivered{0}, touch_releases_delivered{0};
ControlGesture control_gesture;
ControlHitTest control_hit_test = nullptr;
ControlAction control_action = nullptr;
ControlCleanup control_cleanup = nullptr;
lv_obj_t *pending_controls[32]{};
size_t pending_control_count = 0;
uint32_t control_overflows = 0;

void touch_task(void *) {
  bool was_pressed = false;
  int64_t previous = esp_timer_get_time();
  for (;;) {
    const int64_t started = esp_timer_get_time();
    auto gap = static_cast<uint32_t>(started - previous);
    max_touch_gap_us = std::max(max_touch_gap_us.load(), gap);
    previous = started;

    // Only this task accesses M5.Touch. The UI receives complete press/release
    // transitions even if an LVGL draw takes longer than a short tap.
    M5.Touch.update(millis());
    TouchSample sample;
    sample.sampled_us = esp_timer_get_time();
    sample.pressed = M5.Touch.getCount() && M5.Touch.getDetail().isPressed();
    if (sample.pressed) {
      auto detail = M5.Touch.getDetail();
      sample.x = detail.x;
      sample.y = detail.y;
    }
    portENTER_CRITICAL(&touch_lock);
    if (!sample.pressed) {
      sample.x = latest_touch.x;
      sample.y = latest_touch.y;
    }
    latest_touch = sample;
    portEXIT_CRITICAL(&touch_lock);
    if (sample.pressed != was_pressed) {
      if (xQueueSend(touch_events, &sample, 0) != pdTRUE) ++touch_overflows;
      if (sample.pressed) ++touch_presses;
      else ++touch_releases;
      was_pressed = sample.pressed;
    }
    max_touch_poll_us = std::max(max_touch_poll_us.load(),
                                  static_cast<uint32_t>(esp_timer_get_time() - started));
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

uint32_t tick_ms() { return millis(); }

void flush_display(lv_display_t *display, const lv_area_t *area,
                   uint8_t *pixel_map) {
  const int64_t started = esp_timer_get_time();
  const int32_t width = area->x2 - area->x1 + 1;
  const int32_t height = area->y2 - area->y1 + 1;

  // Explicit RGB565 type avoids M5GFX's configurable byte-swap overload.
  M5.Display.pushImage(area->x1, area->y1, width, height,
                       reinterpret_cast<const lgfx::rgb565_t *>(pixel_map));
  max_flush_us = std::max(max_flush_us, static_cast<uint32_t>(esp_timer_get_time() - started));
  ++flushes;
  lv_display_flush_ready(display);
}

void read_touch(lv_indev_t *, lv_indev_data_t *data) {
  TouchSample sample;
  if (xQueueReceive(touch_events, &sample, 0) == pdTRUE) {
    if (sample.pressed) ++touch_delivered;
    else ++touch_releases_delivered;
    max_input_lag_us =
        std::max(max_input_lag_us, static_cast<uint32_t>(esp_timer_get_time() - sample.sampled_us));
    data->continue_reading = uxQueueMessagesWaiting(touch_events) != 0;
  } else {
    portENTER_CRITICAL(&touch_lock);
    sample = latest_touch;
    portEXIT_CRITICAL(&touch_lock);
  }
  data->point.x = sample.x;
  data->point.y = sample.y;
  data->state = sample.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  if (control_hit_test && control_action) {
    // Only look through the LVGL object tree while a gesture is in progress.
    // Repeated released polls still feed the state machine to handle the
    // latest-touch fallback without producing duplicate actions.
    const bool needs_hit = sample.pressed || control_gesture.touching();
    auto *hit = needs_hit ? control_hit_test(sample.x, sample.y) : nullptr;
    auto accepted = control_gesture.sample(sample.pressed, reinterpret_cast<uintptr_t>(hit));
    if (accepted) {
      if (pending_control_count < sizeof(pending_controls) / sizeof(pending_controls[0]))
        pending_controls[pending_control_count++] = reinterpret_cast<lv_obj_t *>(accepted);
      else
        ++control_overflows;
    }
  }
}

}  // namespace

void tab5_lvgl_set_control_handlers(ControlHitTest hit_test, ControlAction action,
                                   ControlCleanup cleanup) {
  control_hit_test = hit_test;
  control_action = action;
  control_cleanup = cleanup;
}

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
  touch_indev = touch;
  touch_events = xQueueCreate(32, sizeof(TouchSample));
  if (!touch_events || xTaskCreate(touch_task, "vega_touch", 4096, nullptr, 3, nullptr) !=
                           pdPASS) {
    Serial.println("LVGL: touch sampler initialization failed");
    return false;
  }

  Serial.printf("LVGL: %ld x %ld, draw buffer %u bytes\n",
                static_cast<long>(width), static_cast<long>(height),
                static_cast<unsigned>(buffer_bytes));
  return true;
}

void tab5_lvgl_update() {
  const int64_t lvgl_started = esp_timer_get_time();
  lv_timer_handler();
  if (control_cleanup && touch_indev &&
      lv_indev_get_state(touch_indev) == LV_INDEV_STATE_RELEASED)
    control_cleanup();
  for (size_t i = 0; i < pending_control_count; ++i)
    control_action(pending_controls[i]);
  pending_control_count = 0;
  max_lvgl_us = std::max(max_lvgl_us,
                         static_cast<uint32_t>(esp_timer_get_time() - lvgl_started));
}

void tab5_lvgl_note_loop(uint32_t loop_gap_us, uint32_t serial_us, uint32_t view_us,
                        uint32_t ui_tick_us) {
  max_loop_gap_us = std::max(max_loop_gap_us, loop_gap_us);
  max_serial_us = std::max(max_serial_us, serial_us);
  max_view_us = std::max(max_view_us, view_us);
  max_ui_tick_us = std::max(max_ui_tick_us, ui_tick_us);
}

void tab5_lvgl_report_perf() {
  Serial.printf("[PERF] touch_gap=%lu touch_poll=%lu input_lag=%lu loop_gap=%lu "
                "lvgl=%lu flush=%lu serial=%lu view=%lu ui_tick=%lu "
                "flushes=%lu presses=%lu delivered=%lu "
                "overflow=%lu queued=%lu us\n",
                static_cast<unsigned long>(max_touch_gap_us.exchange(0)),
                static_cast<unsigned long>(max_touch_poll_us.exchange(0)),
                static_cast<unsigned long>(max_input_lag_us),
                static_cast<unsigned long>(max_loop_gap_us),
                static_cast<unsigned long>(max_lvgl_us),
                static_cast<unsigned long>(max_flush_us),
                static_cast<unsigned long>(max_serial_us),
                static_cast<unsigned long>(max_view_us),
                static_cast<unsigned long>(max_ui_tick_us), static_cast<unsigned long>(flushes),
                static_cast<unsigned long>(touch_presses.load()),
                static_cast<unsigned long>(touch_delivered.load()),
                static_cast<unsigned long>(touch_overflows.load()),
                static_cast<unsigned long>(uxQueueMessagesWaiting(touch_events)));
  max_input_lag_us = max_lvgl_us = max_flush_us = 0;
  max_loop_gap_us = max_serial_us = max_view_us = max_ui_tick_us = flushes = 0;
}

void tab5_lvgl_report_touch_state() {
  TouchSample sample;
  portENTER_CRITICAL(&touch_lock);
  sample = latest_touch;
  portEXIT_CRITICAL(&touch_lock);
  const auto age_ms = sample.sampled_us
                          ? static_cast<unsigned long>((esp_timer_get_time() - sample.sampled_us) / 1000)
                          : 0UL;
  Serial.printf("[TOUCH] sampled_pressed=%u xy=%d,%d age_ms=%lu lvgl_pressed=%u "
                "presses=%lu releases=%lu delivered_presses=%lu delivered_releases=%lu "
                "overflow=%lu queued=%lu control_overflow=%lu\n",
                sample.pressed, sample.x, sample.y, age_ms,
                touch_indev && lv_indev_get_state(touch_indev) == LV_INDEV_STATE_PRESSED,
                static_cast<unsigned long>(touch_presses.load()),
                static_cast<unsigned long>(touch_releases.load()),
                static_cast<unsigned long>(touch_delivered.load()),
                static_cast<unsigned long>(touch_releases_delivered.load()),
                static_cast<unsigned long>(touch_overflows.load()),
                static_cast<unsigned long>(touch_events ? uxQueueMessagesWaiting(touch_events) : 0),
                static_cast<unsigned long>(control_overflows));
}
