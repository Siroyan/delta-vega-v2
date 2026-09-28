#include "tab5_runtime.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <mqtt_client.h>
#include <nvs.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "application/application.h"
#include "application/telemetry_json.h"
#include "course_data.h"
#include "domain/nmea.h"
#include "lvgl_view.h"
#include "../tab5_lvgl.h"
#include "presentation/settings_form.h"

#if __has_include("config/network_secrets.h")
#include "config/network_secrets.h"
#else
#include "config/network_secrets.example.h"
#endif

namespace tab5 {
namespace {
constexpr gpio_num_t kPower = GPIO_NUM_45, kIgnition = GPIO_NUM_48;
constexpr int kReed = 16, kGpsRx = 7, kGpsTx = 6;
QueueHandle_t commands, snapshots, records, transmissions, diagnostics;
std::atomic<bool> urgent_off{false}, sd_ready{false}, sd_error{false}, mqtt_connected{false};
std::atomic<bool> readback{false};
std::atomic<uint64_t> last_ntp_sync{0};
std::atomic<uint32_t> debounce_us{3000};
portMUX_TYPE wheel_lock = portMUX_INITIALIZER_UNLOCKED;
vega::WheelInput wheel_input;
struct Diagnostic {
  char text[160];
};
void diagnostic(const char *format, ...) {
  if (!diagnostics) return;
  Diagnostic message{};
  va_list args;
  va_start(args, format);
  vsnprintf(message.text, sizeof(message.text), format, args);
  va_end(args);
  xQueueSend(diagnostics, &message, 0);  // Logging must not block the control owner.
}
void diagnosticTask(void *) {
  Diagnostic message{};
  for (;;)
    if (xQueueReceive(diagnostics, &message, portMAX_DELAY) == pdTRUE)
      Serial.printf("%s\n", message.text);
}

struct Clock final : vega::IClock {
  vega::Millis now() const override { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }
} clock_source;
bool serialWriteAll(const uint8_t *bytes, size_t n) {
  size_t sent = 0;
  auto until = clock_source.now() + 2000;
  while (sent < n && clock_source.now() < until) {
    sent += Serial.write(bytes + sent, n - sent);
    vTaskDelay(1);
  }
  return sent == n;
}

class EngineOutput final : public vega::IEngineOutput {
 public:
  bool init(bool active_high) {
    active_high_ = active_high;
    // Apply the stored OFF polarity before enabling output drivers.
    gpio_set_level(kPower, !active_high_);
    gpio_set_level(kIgnition, 0);
    gpio_set_direction(kPower, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(kIgnition, GPIO_MODE_INPUT_OUTPUT);
    esp_timer_create_args_t args{};
    args.callback = &EngineOutput::expire;
    args.arg = this;
    args.name = "ecu_pulse";
    return esp_timer_create(&args, &timer_) == ESP_OK;
  }
  void configurePower(bool active_high) override {
    active_high_ = active_high;
    setPower(false);
  }
  void setPower(bool on) override { gpio_set_level(kPower, on == active_high_); }
  bool pulse(uint32_t duration_ms) override {
    if (!timer_) return false;
    // All start/stop and callbacks use the same lock, including the generation check.
    esp_timer_stop(timer_);
    portENTER_CRITICAL(&lock_);
    armed_ = true;
    deadline_us_ = esp_timer_get_time() + static_cast<int64_t>(duration_ms) * 1000;
    gpio_set_level(kIgnition, 1);
    portEXIT_CRITICAL(&lock_);
    if (esp_timer_start_once(timer_, static_cast<uint64_t>(duration_ms) * 1000) != ESP_OK) {
      stopPulse();
      return false;
    }
    return true;
  }
  void stopPulse() override {
    if (timer_) esp_timer_stop(timer_);
    portENTER_CRITICAL(&lock_);
    armed_ = false;
    gpio_set_level(kIgnition, 0);
    portEXIT_CRITICAL(&lock_);
  }

 private:
  static void expire(void *arg) {
    auto &self = *static_cast<EngineOutput *>(arg);
    portENTER_CRITICAL(&self.lock_);
    // A delayed callback from an earlier pulse must not shorten a new pulse.
    if (self.armed_ && esp_timer_get_time() >= self.deadline_us_) {
      self.armed_ = false;
      gpio_set_level(kIgnition, 0);
    }
    portEXIT_CRITICAL(&self.lock_);
  }
  esp_timer_handle_t timer_ = nullptr;
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  int64_t deadline_us_ = 0;
  bool armed_ = false, active_high_ = true;
} engine_output;
bool outputs_prepared = false;

class SettingsStore final : public vega::ISettingsStore {
 public:
  vega::Settings load() {
    vega::Settings s;
    nvs_handle_t handle;
    if (nvs_open("vega", NVS_READONLY, &handle) == ESP_OK) {
      vega::Settings stored;
      size_t size = sizeof(stored);
      if (nvs_get_blob(handle, "settings", &stored, &size) == ESP_OK && size == sizeof(stored) &&
          vega::validSettings(stored))
        s = stored;
      nvs_close(handle);
    }
    return s;
  }
  bool save(const vega::Settings &s) override {
    Preferences p;
    if (!p.begin("vega", false)) return false;
    bool ok = p.putBytes("settings", &s, sizeof(s)) == sizeof(s);
    p.end();
    return ok;
  }
} settings_store;

// SD I/O happens exclusively in the writer task. Queue overflow is surfaced as a warning.
enum class RecordKind : uint8_t { Begin, Sample, Event, End };
struct Record {
  RecordKind kind;
  vega::Snapshot data;
  vega::Event event{};
  vega::EndReason reason{};
};
void enqueueRecord(const Record &r) {
  if (xQueueSend(records, &r, 0) != pdTRUE) {
    sd_error = true;
    diagnostic("[SD] queue full; record lost, timing continues");
  }
}
struct Recorder final : vega::ISessionRecorder {
  void begin(uint32_t session, const vega::Settings &s, vega::Millis now) override {
    Record r{};
    r.kind = RecordKind::Begin;
    r.data.settings = s;
    r.data.now_ms = now;
    r.data.race.session = session;
    enqueueRecord(r);
  }
  void sample(const vega::Snapshot &s) override {
    Record r{};
    r.kind = RecordKind::Sample;
    r.data = s;
    enqueueRecord(r);
  }
  void event(vega::Event e, const vega::Snapshot &s) override {
    Record r{};
    r.kind = RecordKind::Event;
    r.data = s;
    r.event = e;
    enqueueRecord(r);
  }
  void end(vega::EndReason e, const vega::Snapshot &s) override {
    Record r{};
    r.kind = RecordKind::End;
    r.data = s;
    r.reason = e;
    enqueueRecord(r);
  }
} recorder;
struct Telemetry final : vega::ITelemetry {
  void publish(const vega::Snapshot &s) override {
    // Only latest live data, no disconnected backfill.
    if (mqtt_connected) xQueueOverwrite(transmissions, &s);
  }
} telemetry;

void IRAM_ATTR reedInterrupt() {
  uint64_t now = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&wheel_lock);
  if (!wheel_input.last_pulse_us ||
      now - wheel_input.last_pulse_us >= debounce_us.load(std::memory_order_relaxed)) {
    wheel_input.previous_pulse_us = wheel_input.last_pulse_us;
    wheel_input.last_pulse_us = now;
    ++wheel_input.pulses;
  }
  portEXIT_CRITICAL_ISR(&wheel_lock);
}

const char *eventName(vega::Event e) {
  switch (e) {
    case vega::Event::Started:
      return "started";
    case vega::Event::Cancelled:
      return "cancelled";
    case vega::Event::Finished:
      return "finished";
    case vega::Event::ManualLap:
      return "manual_lap";
    case vega::Event::GpsLap:
      return "gps_lap";
    case vega::Event::PowerOn:
      return "power_on";
    case vega::Event::PowerOff:
      return "power_off";
    case vega::Event::Ignition:
      return "ignition";
    case vega::Event::SettingsSaved:
      return "settings_saved";
  }
  return "unknown";
}

// Checked POSIX I/O exposes flush/fsync failures that Arduino File::flush hides.
class SdFile {
 public:
  SdFile() = default;
  SdFile(const char *path, const char *mode) {
    char full[120];
    snprintf(full, sizeof(full), "/sdcard%s", path);
    file_ = fopen(full, mode);
    if (file_) setvbuf(file_, nullptr, _IONBF, 0);
  }
  ~SdFile() { close(); }
  SdFile(const SdFile &) = delete;
  SdFile &operator=(const SdFile &) = delete;
  SdFile(SdFile &&other) noexcept : file_(other.file_) { other.file_ = nullptr; }
  SdFile &operator=(SdFile &&other) noexcept {
    if (this != &other) {
      close();
      file_ = other.file_;
      other.file_ = nullptr;
    }
    return *this;
  }
  explicit operator bool() const { return file_ != nullptr; }
  size_t write(const uint8_t *bytes, size_t n) { return file_ ? fwrite(bytes, 1, n, file_) : 0; }
  size_t write(char c) { return write(reinterpret_cast<const uint8_t *>(&c), 1); }
  size_t read(uint8_t *bytes, size_t n) { return file_ ? fread(bytes, 1, n, file_) : 0; }
  uint64_t size() {
    struct stat s {};
    return file_ && fstat(fileno(file_), &s) == 0 ? s.st_size : 0;
  }
  bool flush() {
    return file_ && fflush(file_) == 0 && !ferror(file_) && fsync(fileno(file_)) == 0;
  }
  void close() {
    if (file_) {
      fclose(file_);
      file_ = nullptr;
    }
  }

 private:
  FILE *file_ = nullptr;
};

void sdTask(void *) {
  SD_MMC.setPins(43, 44, 39, 40, 41, 42);
  sd_ready = SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_DEFAULT);
  if (sd_ready) {
    if (!SD_MMC.exists("/vega")) sd_ready = SD_MMC.mkdir("/vega");
  }
  sd_error = !sd_ready;
  Serial.printf("[SD] mount=%s card_bytes=%llu\n", sd_ready ? "OK" : "FAILED",
                sd_ready ? SD_MMC.cardSize() : 0ULL);
  SdFile log;
  char last_path[80]{};
  uint64_t last_flush = 0;
  bool active = false, failed_session = false;
  for (;;) {
    Record r{};
    if (xQueueReceive(records, &r, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (r.kind == RecordKind::Begin) {
        if (log) log.close();
        active = true;
        failed_session = false;
        // Persistent monotonic ID plus existence checks prevents overwriting on reboot.
        Preferences p;
        uint32_t number = 0;
        if (p.begin("vega-log", false)) {
          number = p.getUInt("next", 0) + 1;
          p.putUInt("next", number);
          p.end();
        }
        do {
          snprintf(last_path, sizeof(last_path), "/vega/session-%010lu.jsonl",
                   static_cast<unsigned long>(number++));
        } while (sd_ready && SD_MMC.exists(last_path));
        if (sd_ready) log = SdFile(last_path, "w");
        if (!log) {
          failed_session = true;
          sd_error = true;
          Serial.println("[SD] cannot open session");
        } else {
          // Metadata is a distinct line; sample lines preserve the MQTT schema.
          char meta[1100];
          const auto &s = r.data.settings;
          int n = snprintf(
              meta, sizeof(meta),
              "{\"type\":\"session\",\"schema_version\":1,\"boot_session\":%lu,\"started_uptime_"
              "ms\":%llu,\"total_target_s\":%lu,\"lap_target_s\":[%lu,%lu,%lu,%lu,%lu,%lu,%lu],"
              "\"start\":[%.8f,%.8f],\"timing\":[%.8f,%.8f],\"goal\":[%.8f,%.8f]}\n",
              static_cast<unsigned long>(r.data.race.session), r.data.now_ms,
              static_cast<unsigned long>(s.total_target_s),
              static_cast<unsigned long>(s.lap_target_s[0]),
              static_cast<unsigned long>(s.lap_target_s[1]),
              static_cast<unsigned long>(s.lap_target_s[2]),
              static_cast<unsigned long>(s.lap_target_s[3]),
              static_cast<unsigned long>(s.lap_target_s[4]),
              static_cast<unsigned long>(s.lap_target_s[5]),
              static_cast<unsigned long>(s.lap_target_s[6]), s.start.latitude, s.start.longitude,
              s.timing.latitude, s.timing.longitude, s.goal.latitude, s.goal.longitude);
          if (n <= 0 || static_cast<size_t>(n) >= sizeof(meta) ||
              log.write(reinterpret_cast<const uint8_t *>(meta), n) != static_cast<size_t>(n))
            failed_session = true;
          Serial.printf("[SD] session opened %s\n", last_path);
        }
      } else if (active && log && !failed_session) {
        char line[1100]{};
        size_t n = 0;
        if (r.kind == RecordKind::Sample)
          n = vega::telemetryJson(r.data, line, sizeof(line), network_config::machine_id,
                                  network_config::memo);
        else {
          const char *e =
              r.kind == RecordKind::End
                  ? (r.reason == vega::EndReason::Finished ? "end_finished" : "end_cancelled")
                  : eventName(r.event);
          int count = snprintf(line, sizeof(line),
                               "{\"type\":\"event\",\"event\":\"%s\",\"timestamp_ms\":%llu,\"total_"
                               "time_ms\":%llu,\"lap_number\":%u}",
                               e, r.data.now_ms, r.data.race.total_ms, r.data.race.lap);
          if (count > 0 && static_cast<size_t>(count) < sizeof(line)) n = count;
        }
        if (!n || log.write(reinterpret_cast<const uint8_t *>(line), n) != n ||
            log.write('\n') != 1)
          failed_session = true;
      }
      if (failed_session) {
        sd_error = true;
        if (log) log.close();
      }
      if (r.kind == RecordKind::End) {
        if (log) {
          if (!log.flush()) {
            failed_session = true;
            sd_error = true;
          }
          log.close();
        }
        active = false;
        Serial.printf("[SD] session closed %s error=%u\n", last_path, failed_session);
      }
    }
    auto now = clock_source.now();
    if (log && now - last_flush >= 1000) {
      if (!log.flush()) {
        failed_session = true;
        sd_error = true;
        log.close();
        Serial.println("[SD] flush failed; timing continues");
      }
      last_flush = now;
    }
    if (readback.exchange(false)) {
      if (active) {
        Serial.println("[SD READBACK] cancel or finish timing before reading logs");
        continue;
      }
      if (log && !log.flush()) {
        sd_error = true;
        failed_session = true;
        log.close();
      }
      SdFile file = sd_ready && last_path[0] ? SdFile(last_path, "r") : SdFile();
      if (!file)
        Serial.println("[SD READBACK] no readable session");
      else {
        char header[140];
        int count = snprintf(header, sizeof(header), "[SD READBACK BEGIN] %s bytes=%llu\n",
                             last_path, static_cast<uint64_t>(file.size()));
        serialWriteAll(reinterpret_cast<const uint8_t *>(header), count);
        uint8_t bytes[256];
        size_t n;
        while ((n = file.read(bytes, sizeof(bytes))) > 0) {
          if (!serialWriteAll(bytes, n)) {
            Serial.println("[SD READBACK] USB transmit timeout");
            break;
          }
        }
        file.close();
        constexpr char end[] = "[SD READBACK END]\n";
        serialWriteAll(reinterpret_cast<const uint8_t *>(end), sizeof(end) - 1);
      }
    }
  }
}

void mqttEvent(void *, esp_event_base_t, int32_t id, void *) {
  if (id == MQTT_EVENT_CONNECTED) {
    mqtt_connected = true;
    Serial.println("[MQTT] connected");
  } else if (id == MQTT_EVENT_DISCONNECTED || id == MQTT_EVENT_ERROR)
    mqtt_connected = false;
}
void ntpSynced(struct timeval *) { last_ntp_sync = clock_source.now(); }
void networkTask(void *) {
  const bool wifi_configured = network_config::ssid[0];
  const bool mqtt_configured =
      network_config::root_ca[0] && network_config::client_cert[0] && network_config::client_key[0];
  esp_mqtt_client_handle_t client = nullptr;
  if (wifi_configured) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(network_config::ssid, network_config::password);
    sntp_set_time_sync_notification_cb(ntpSynced);
    configTime(9 * 3600, 0, network_config::ntp_server);
    if (mqtt_configured) {
      esp_mqtt_client_config_t cfg{};
      cfg.broker.address.uri = network_config::endpoint;
      cfg.broker.verification.certificate = network_config::root_ca;
      cfg.credentials.client_id = network_config::client_id;
      cfg.credentials.authentication.certificate = network_config::client_cert;
      cfg.credentials.authentication.key = network_config::client_key;
      cfg.network.disable_auto_reconnect = false;
      cfg.session.disable_clean_session = false;
      cfg.outbox.limit = 4096;
      client = esp_mqtt_client_init(&cfg);
      if (client) {
        esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqttEvent, nullptr);
        esp_mqtt_client_start(client);
      }
    }
  } else
    Serial.println("[NET] Wi-Fi not configured; MQTT/NTP offline");
  for (;;) {
    vega::Snapshot s{};
    if (xQueueReceive(transmissions, &s, pdMS_TO_TICKS(500)) == pdTRUE && client &&
        mqtt_connected) {
      char payload[1100];
      auto n = vega::telemetryJson(s, payload, sizeof(payload), network_config::machine_id,
                                   network_config::memo);
      if (n && clock_source.now() - s.now_ms < 1500)
        esp_mqtt_client_enqueue(client, network_config::topic, payload, n, 0, 0, false);
    }
  }
}

void applicationTask(void *) {
  vega::Course course(course_data);
  auto settings = settings_store.load();
  debounce_us = settings.pulse_debounce_us;
  vega::Application app(clock_source, engine_output, settings_store, recorder, telemetry, course,
                        settings);
  HardwareSerial gps_uart(1);
  gps_uart.setRxBufferSize(2048);
  gps_uart.begin(9600, SERIAL_8N1, kGpsRx, kGpsTx);
  pinMode(kReed, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kReed), reedInterrupt, FALLING);
  vega::NmeaParser parser;
  diagnostic("[APP] ready; electrical OFF; commands: status/start/cancel/lap/on/off/ignite/log");
  for (;;) {
    if (urgent_off.exchange(false)) {
      xQueueReset(commands);  // Earlier queued ON/IGNITE must never undo an explicit OFF.
      app.power(false);
      diagnostic("[APP] electrical OFF; pending commands cleared");
    }
    vega::WheelInput input;
    portENTER_CRITICAL(&wheel_lock);
    input = wheel_input;
    portEXIT_CRITICAL(&wheel_lock);
    app.wheel(input);
    for (unsigned budget = 0; budget < 512 && gps_uart.available(); ++budget) {
      vega::GpsFix fix;
      if (parser.feed(static_cast<char>(gps_uart.read()), clock_source.now(), fix)) app.gps(fix);
    }
    Command command{};
    for (unsigned budget = 0; budget < 8 && xQueueReceive(commands, &command, 0) == pdTRUE;
         ++budget) {
      bool ok = true;
      switch (command.kind) {
        case CommandKind::Start:
          ok = app.start();
          break;
        case CommandKind::Cancel:
          ok = app.cancel();
          break;
        case CommandKind::Lap:
          ok = app.manualLap();
          break;
        case CommandKind::PowerOn:
          app.power(true);
          break;
        case CommandKind::PowerOff:
          app.power(false);
          break;
        case CommandKind::Ignite:
          ok = app.ignite();
          break;
        case CommandKind::Configure:
          ok = app.configure(command.settings);
          if (ok) debounce_us = command.settings.pulse_debounce_us;
          break;
      }
      diagnostic("[APP] command=%u accepted=%u", static_cast<unsigned>(command.kind), ok);
    }
    app.tick();
    auto s = app.snapshot();
    xQueueOverwrite(snapshots, &s);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}  // namespace

bool begin() {
  if (!outputs_prepared && !prepareOutputs()) return false;
  commands = xQueueCreate(12, sizeof(Command));
  snapshots = xQueueCreate(1, sizeof(vega::Snapshot));
  records = xQueueCreate(48, sizeof(Record));
  transmissions = xQueueCreate(1, sizeof(vega::Snapshot));
  diagnostics = xQueueCreate(16, sizeof(Diagnostic));
  if (!commands || !snapshots || !records || !transmissions || !diagnostics) return false;
  if (xTaskCreate(diagnosticTask, "vega_log", 3072, nullptr, 1, nullptr) != pdPASS ||
      xTaskCreate(sdTask, "vega_sd", 8192, nullptr, 1, nullptr) != pdPASS ||
      xTaskCreate(networkTask, "vega_net", 8192, nullptr, 1, nullptr) != pdPASS ||
      xTaskCreate(applicationTask, "vega_app", 8192, nullptr, 4, nullptr) != pdPASS)
    return false;
  return true;
}
bool prepareOutputs() {
  auto settings = settings_store.load();
  outputs_prepared = engine_output.init(settings.power_active_high);
  return outputs_prepared;
}
void outputsOff() {
  engine_output.stopPulse();
  engine_output.setPower(false);
}
bool submit(const Command &c) {
  if (!commands) return false;
  if (c.kind == CommandKind::PowerOff) {
    urgent_off = true;
    return true;
  }
  return xQueueSend(commands, &c, 0) == pdTRUE;
}
bool snapshot(vega::Snapshot &s) { return snapshots && xQueuePeek(snapshots, &s, 0) == pdTRUE; }
vega::UiStatus status() {
  vega::UiStatus s;
  s.sd_ready = sd_ready;
  s.sd_error = sd_error;
  s.mqtt_connected = mqtt_connected;
  s.network_configured = network_config::ssid[0];
  auto synced = last_ntp_sync.load();
  s.time_valid = synced != 0;
  s.ntp_holdover = s.time_valid && (WiFi.status() != WL_CONNECTED ||
                                    clock_source.now() - synced > 2 * 3600 * 1000ULL);
  if (s.time_valid) {
    time_t now = time(nullptr);
    struct tm local {};
    localtime_r(&now, &local);
    strftime(s.clock, sizeof(s.clock), "%H:%M:%S JST", &local);
  }
  return s;
}
void requestLogReadback() { readback = true; }
void serialPoll() {
  static char buffer[64];
  static size_t count = 0;
  for (unsigned budget = 0; budget < 128 && Serial.available(); ++budget) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buffer[count] = 0;
      count = 0;
      if (viewDiagnostic(buffer)) continue;
      if (!strcmp(buffer, "status")) {
        vega::Snapshot s;
        auto st = status();
        if (snapshot(s))
          Serial.printf(
              "[STATUS] phase=%u lap=%u total_ms=%llu power_phase=%u power_pin=%d ignition_pin=%d "
              "gps=%u pulses=%llu sd_ready=%u sd_error=%u mqtt=%u ntp=%u total_target_s=%lu "
              "lap1_target_s=%lu\n",
              static_cast<unsigned>(s.race.phase), s.race.lap, s.race.total_ms,
              static_cast<unsigned>(s.engine), gpio_get_level(kPower), gpio_get_level(kIgnition),
              s.gps_fresh, s.wheel.pulses, st.sd_ready, st.sd_error, st.mqtt_connected,
              st.time_valid, static_cast<unsigned long>(s.settings.total_target_s),
              static_cast<unsigned long>(s.settings.lap_target_s[0]));
      } else if (!strcmp(buffer, "settings")) {
        vega::Snapshot s;
        if (snapshot(s))
          for (size_t i = 0; i < vega::kSettingsFieldCount; ++i) {
            char value[32];
            vega::settingText(s.settings, i, value, sizeof(value));
            Serial.printf("[SETTINGS] %u %s = %s\n", static_cast<unsigned>(i),
                          vega::settingTitle(i), value);
            vTaskDelay(1);
          }
      } else if (!strncmp(buffer, "config ", 7)) {
        char key[32], value[24];
        vega::Snapshot s;
        if (sscanf(buffer + 7, "%31s %23s", key, value) == 2 && snapshot(s)) {
          Command c{};
          c.kind = CommandKind::Configure;
          c.settings = s.settings;
          char *end = nullptr;
          double number = strtod(value, &end);
          bool known = true;
          if (!end || *end || !std::isfinite(number) || number < 0)
            known = false;
          else if (!strcmp(key, "power_active_high")) {
            if (number == 0 || number == 1)
              c.settings.power_active_high = number == 1;
            else
              known = false;
          } else if (!strcmp(key, "wheel_circumference_m"))
            c.settings.wheel_circumference_m = number;
          else if (!strcmp(key, "course_corridor_m"))
            c.settings.course_corridor_m = number;
          else if (!strcmp(key, "max_gps_step_m"))
            c.settings.max_gps_step_m = number;
          else if (!strcmp(key, "min_lap_progress_m"))
            c.settings.min_lap_progress_m = number;
          else if (number > UINT32_MAX || number != std::floor(number))
            known = false;
          else if (!strcmp(key, "ecu_ready_ms"))
            c.settings.ecu_ready_ms = number;
          else if (!strcmp(key, "ignition_pulse_ms"))
            c.settings.ignition_pulse_ms = number;
          else if (!strcmp(key, "pulse_debounce_us"))
            c.settings.pulse_debounce_us = number;
          else if (!strcmp(key, "speed_zero_ms"))
            c.settings.speed_zero_ms = number;
          else if (!strcmp(key, "gps_stale_ms"))
            c.settings.gps_stale_ms = number;
          else if (!strcmp(key, "min_lap_ms"))
            c.settings.min_lap_ms = number;
          else if (!strcmp(key, "lap_duplicate_ms"))
            c.settings.lap_duplicate_ms = number;
          else if (!strcmp(key, "pulses_per_revolution"))
            c.settings.pulses_per_revolution = number;
          else
            known = false;
          Serial.printf("[CONFIG] queued=%u\n",
                        known && vega::validSettings(c.settings) && submit(c));
        } else
          Serial.println("[CONFIG] use config KEY VALUE");
      } else if (!strcmp(buffer, "perf"))
        tab5_lvgl_report_perf();
      else if (!strcmp(buffer, "log"))
        requestLogReadback();
      else {
        Command command{};
        bool known = true;
        if (!strcmp(buffer, "start"))
          command.kind = CommandKind::Start;
        else if (!strcmp(buffer, "cancel"))
          command.kind = CommandKind::Cancel;
        else if (!strcmp(buffer, "lap"))
          command.kind = CommandKind::Lap;
        else if (!strcmp(buffer, "on"))
          command.kind = CommandKind::PowerOn;
        else if (!strcmp(buffer, "off"))
          command.kind = CommandKind::PowerOff;
        else if (!strcmp(buffer, "ignite"))
          command.kind = CommandKind::Ignite;
        else
          known = false;
        if (known)
          Serial.printf("[CMD] queued=%u\n", submit(command));
        else if (buffer[0])
          Serial.println("[CMD] unknown command");
      }
    } else if (count + 1 < sizeof(buffer))
      buffer[count++] = c;
    else
      count = 0;
  }
}
}  // namespace tab5
