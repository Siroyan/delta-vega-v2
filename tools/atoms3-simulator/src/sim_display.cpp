#include "sim_display.h"

#include <Arduino.h>
#include <M5Unified.h>

namespace sim_display {
namespace {

constexpr uint16_t kBackground = 0x0844;
constexpr uint16_t kPanel = 0x10C8;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kMuted = 0xAD55;
constexpr uint16_t kGreen = 0x05E9;
constexpr uint16_t kAmber = 0xFE20;
constexpr uint16_t kCyan = 0x3DFF;
constexpr uint16_t kRed = 0xF986;
bool ready = false;
uint32_t next_draw_ms = 0;

M5Canvas &canvas() {
  static M5Canvas instance(&M5.Display);
  return instance;
}

const char *modeLabel(Mode mode) {
  switch (mode) {
    case Mode::Idle: return "READY";
    case Mode::Running: return "RUN";
    case Mode::Paused: return "PAUSE";
    case Mode::Finished: return "GOAL";
    case Mode::Stopped: return "STOP";
  }
  return "?";
}

uint16_t modeColor(Mode mode) {
  switch (mode) {
    case Mode::Idle: return kAmber;
    case Mode::Running: return kGreen;
    case Mode::Paused: return kCyan;
    case Mode::Finished: return kGreen;
    case Mode::Stopped: return kAmber;
  }
  return kMuted;
}

} // namespace

void begin() {
  auto config = M5.config();
  config.serial_baudrate = 0; // USB console belongs to the simulator.
  config.external_display_value = 0;
  config.internal_imu = false;
  config.internal_rtc = false;
  config.internal_mic = false;
  config.internal_spk = false;
  M5.begin(config);
  if (M5.Display.width() != 128 || M5.Display.height() != 128) {
    Serial.printf("[SIM] display unavailable (%dx%d)\n",
                  M5.Display.width(), M5.Display.height());
    return;
  }
  M5.Display.setBrightness(160);
  canvas().setColorDepth(16);
  ready = canvas().createSprite(128, 128) != nullptr;
  Serial.printf("[SIM] display %s\n", ready ? "ready" : "buffer allocation failed");
}

void update(const Snapshot &snapshot, uint32_t now_ms) {
  if (!ready || static_cast<int32_t>(now_ms - next_draw_ms) < 0) return;
  next_draw_ms = now_ms + 500;

  auto &view = canvas();
  view.fillSprite(kBackground);
  view.fillRoundRect(3, 3, 122, 19, 4, kPanel);
  view.setTextFont(1);
  view.setTextSize(1);
  view.setTextDatum(textdatum_t::middle_left);
  view.setTextColor(kCyan);
  view.drawString("ATOM SIM", 8, 13);
  view.setTextDatum(textdatum_t::middle_right);
  view.setTextColor(modeColor(snapshot.mode));
  view.drawString(modeLabel(snapshot.mode), 120, 13);

  view.setTextDatum(textdatum_t::middle_center);
  view.setTextColor(kText);
  view.setTextSize(2);
  view.drawString(snapshot.course_name, 64, 38);
  char line[32];
  if (snapshot.lap_count >= 100)
    snprintf(line, sizeof(line), "%u / %u", snapshot.lap, snapshot.lap_count);
  else
    snprintf(line, sizeof(line), "LAP %u/%u", snapshot.lap, snapshot.lap_count);
  view.drawString(line, 64, 62);

  snprintf(line, sizeof(line), "%.1f", snapshot.speed_kmh);
  view.setTextColor(modeColor(snapshot.mode));
  view.drawString(line, 64, 84);
  view.setTextSize(1);
  view.setTextColor(kMuted);
  view.drawString(snapshot.mode == Mode::Running ? "km/h" : "SET km/h", 64, 100);

  view.setTextDatum(textdatum_t::middle_left);
  view.setTextColor(kText);
  if (snapshot.mode == Mode::Idle) {
    view.drawString("TAP GO   HOLD NEXT", 5, 112);
  } else if (snapshot.mode == Mode::Paused) {
    view.drawString("TAP RUN  HOLD RESET", 5, 112);
  } else if (snapshot.mode == Mode::Finished || snapshot.mode == Mode::Stopped) {
    view.drawString("TAP RESET", 5, 112);
  } else {
    snprintf(line, sizeof(line), "D %.0fm", snapshot.distance_m);
    view.drawString(line, 5, 112);
    view.setTextDatum(textdatum_t::middle_right);
    snprintf(line, sizeof(line), "P %llu", static_cast<unsigned long long>(snapshot.pulses));
    view.drawString(line, 123, 112);
  }

  view.setTextDatum(textdatum_t::middle_left);
  view.setTextColor(snapshot.gps_status[0] == 'O' ? kGreen : kAmber);
  view.drawString(snapshot.gps_status, 5, 122);
  view.setTextDatum(textdatum_t::middle_right);
  view.setTextColor(snapshot.missed_pulses ? kRed : kMuted);
  snprintf(line, sizeof(line), "MISS %u", snapshot.missed_pulses);
  view.drawString(line, 123, 122);

  // The progress strip follows the active route, including the final branch.
  view.fillRect(3, 126, 122, 2, kPanel);
  const int progress = static_cast<int>(snapshot.route_fraction * 122);
  if (progress > 0) view.fillRect(3, 126, progress > 122 ? 122 : progress, 2, kCyan);
  view.pushSprite(0, 0);
}

} // namespace sim_display
