#include "tab5_audio.h"

#include <Arduino.h>
#include <M5Unified.h>

namespace tab5 {
namespace {
enum class Sound : uint8_t { Button, Strategy };
QueueHandle_t sounds = nullptr;

void playTone(uint32_t duration_ms) {
  M5.Speaker.tone(2200, duration_ms);
  vTaskDelay(pdMS_TO_TICKS(duration_ms));
}

void audioTask(void *) {
  M5.Speaker.setVolume(96);
  Sound sound;
  for (;;) {
    if (xQueueReceive(sounds, &sound, portMAX_DELAY) != pdTRUE) continue;
    if (sound == Sound::Button) {
      playTone(55);
      continue;
    }
    // Two short pairs: "pi-pi, pi-pi". No wait occurs in the UI/app task.
    for (int pair = 0; pair < 2; ++pair) {
      playTone(90);
      vTaskDelay(pdMS_TO_TICKS(90));
      playTone(90);
      if (pair == 0) vTaskDelay(pdMS_TO_TICKS(320));
    }
  }
}
}  // namespace

bool audioBegin() {
  sounds = xQueueCreate(8, sizeof(Sound));
  if (!sounds) return false;
  if (xTaskCreate(audioTask, "vega_audio", 4096, nullptr, 1, nullptr) != pdPASS) {
    vQueueDelete(sounds);
    sounds = nullptr;
    return false;
  }
  return true;
}

void audioButton() {
  if (!sounds) return;
  const Sound sound = Sound::Button;
  xQueueSend(sounds, &sound, 0);
}

void audioStrategyWarning() {
  if (!sounds) return;
  const Sound sound = Sound::Strategy;
  // Put the driving cue ahead of queued taps. Only discard a tap if full.
  if (xQueueSendToFront(sounds, &sound, 0) != pdTRUE) {
    Sound oldest;
    xQueueReceive(sounds, &oldest, 0);
    xQueueSendToFront(sounds, &sound, 0);
  }
}
}  // namespace tab5
