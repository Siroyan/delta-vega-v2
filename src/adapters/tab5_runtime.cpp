#include "tab5_runtime.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <mbedtls/platform.h>
#include <mqtt_client.h>
#include <nvs.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "application/application.h"
#include "application/telemetry_json.h"
#include "course_catalog.h"
#include "domain/nmea.h"
#include "domain/settings_codec.h"
#include "lvgl_view.h"
#include "reed_pulse_filter.h"
#include "settings_migration.h"
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
constexpr int kReed = 16;
// A hand-operated jumper or reed contact can briefly reopen during one closure.
// Require a continuously HIGH (open) interval before counting another wheel pass.
constexpr uint32_t kReedReleaseUs = 30000;
constexpr int kGpsBusRx = 7, kGpsBusTx = 6;
// Unit GPS: yellow (unit RX) to G53, white (unit TX) to G54 on Tab5 Port.A.
constexpr int kGpsPortARx = 54, kGpsPortATx = 53;

// The bundled ESP-IDF builds mbedTLS for internal RAM. A TLS handshake can
// exhaust the DMA-capable heap needed by ESP-Hosted Wi-Fi on Tab5. Install the
// supported mbedTLS allocator hook before any network task starts.
void *tlsPsramCalloc(size_t count, size_t bytes) {
  return heap_caps_calloc(count, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void tlsPsramFree(void *ptr) { heap_caps_free(ptr); }
QueueHandle_t commands, snapshots, records, transmissions, diagnostics, strategies,
    plan_imports, plan_requests, plan_choices;
std::atomic<uint8_t> active_course{0};
struct PlanRequest {
  uint8_t course_index;
  char filename[48];
};
std::atomic<bool> urgent_off{false}, sd_ready{false}, sd_error{false}, mqtt_connected{false};
std::atomic<uint32_t> power_epoch{0};
std::atomic<uint32_t> record_loss_count{0};
enum class SdFailure : uint8_t { None, QueueFull, Open, Metadata, Write, Flush, EndFlush, ReadbackFlush };
std::atomic<SdFailure> sd_last_failure{SdFailure::None};
const char *sdFailureText(SdFailure reason) {
  switch (reason) {
    case SdFailure::QueueFull: return "queue_full";
    case SdFailure::Open: return "open";
    case SdFailure::Metadata: return "metadata";
    case SdFailure::Write: return "write";
    case SdFailure::Flush: return "flush";
    case SdFailure::EndFlush: return "end_flush";
    case SdFailure::ReadbackFlush: return "readback_flush";
    default: return "none";
  }
}
std::atomic<uint32_t> gps_bytes{0}, gps_rmc{0};
std::atomic<uint32_t> gps_rmc_hz_x10{0};
std::atomic<bool> gps_nav_query_requested{false};
std::atomic<bool> gps_walk_requested{false};
std::atomic<vega::PlanState> plan_state{vega::PlanState::Loading};
std::atomic<bool> readback{false};
std::atomic<uint32_t> readback_session{0};
std::atomic<uint64_t> last_ntp_sync{0};
std::atomic<uint32_t> debounce_us{3000};
portMUX_TYPE wheel_lock = portMUX_INITIALIZER_UNLOCKED;
vega::WheelInput wheel_input;
ReedPulseFilter reed_filter;
struct Diagnostic {
  char text[160];
};
struct PlanImport {
  char *contents;
  size_t length;
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

// AT6558/CASIC NMEA command. Configure only the known Port.A Unit GPS;
// the M5Bus receiver uses a different, as yet unconfirmed command set.
bool sendCasic(HardwareSerial &uart, const char *payload) {
  uint8_t checksum = 0;
  for (const char *p = payload; *p; ++p) checksum ^= static_cast<uint8_t>(*p);
  char sentence[96];
  const int length = snprintf(sentence, sizeof(sentence), "$%s*%02X\r\n", payload, checksum);
  return length > 0 && static_cast<size_t>(length) < sizeof(sentence) &&
         uart.write(reinterpret_cast<const uint8_t *>(sentence), length) ==
             static_cast<size_t>(length);
}

// CASIC CFG-NAVX poll: header, empty payload, class 0x06, id 0x07,
// 32-bit little-endian additive checksum (0x07060000).
bool requestCasicNavx(HardwareSerial &uart) {
  constexpr uint8_t query[] = {0xBA, 0xCE, 0, 0, 0x06, 0x07, 0, 0, 0x06, 0x07};
  return uart.write(query, sizeof(query)) == sizeof(query);
}

// Apply only dynModel=2 (walking) in receiver RAM. The mask leaves all other
// navigation parameters unchanged, and no CASIC save command is sent.
bool requestCasicWalking(HardwareSerial &uart) {
  uint8_t frame[54] = {0xBA, 0xCE, 44, 0, 0x06, 0x07};
  frame[6] = 1;   // mask bit 0: apply dynamic model
  frame[10] = 2;  // walking mode
  const uint32_t checksum = 0x0706002C + 1 + 2;
  for (size_t i = 0; i < 4; ++i) frame[50 + i] = uint8_t(checksum >> (8 * i));
  return uart.write(frame, sizeof(frame)) == sizeof(frame);
}

uint32_t casicWord(const uint8_t *bytes) {
  return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
         (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

struct Clock final : vega::IClock {
  vega::Millis now() const override { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }
  uint64_t nowMicros() const override { return static_cast<uint64_t>(esp_timer_get_time()); }
} clock_source;
struct QueuedCommand {
  Command command;
  uint32_t power_epoch;
};
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

struct SavedSelection {
  uint32_t version = 1;
  char course_id[48]{};
  char plan[48]{};
};
SavedSelection loadSelection() {
  SavedSelection value{};
  Preferences p;
  if (p.begin("vega-select", true)) {
    if (p.getBytesLength("choice") == sizeof(value)) {
      SavedSelection stored{};
      if (p.getBytes("choice", &stored, sizeof(stored)) == sizeof(stored) &&
          stored.version == 1 && memchr(stored.course_id, 0, sizeof(stored.course_id)) &&
          memchr(stored.plan, 0, sizeof(stored.plan))) value = stored;
    }
    p.end();
  }
  return value;
}
uint8_t savedCourseIndex() {
  const auto selection = loadSelection();
  return static_cast<uint8_t>(courseIndex(selection.course_id));
}
void savedPlanName(char *out, size_t size) {
  const auto selection = loadSelection();
  snprintf(out, size, "%s", selection.plan);
}
bool saveSelection(const char *course_id, const char *plan) {
  SavedSelection value{};
  if (strlen(course_id) >= sizeof(value.course_id) || strlen(plan) >= sizeof(value.plan))
    return false;
  snprintf(value.course_id, sizeof(value.course_id), "%s", course_id);
  snprintf(value.plan, sizeof(value.plan), "%s", plan);
  Preferences p;
  if (!p.begin("vega-select", false)) return false;
  const bool ok = p.putBytes("choice", &value, sizeof(value)) == sizeof(value);
  p.end();
  return ok;
}
class SettingsStore final : public vega::ISettingsStore {
 public:
  void select(uint8_t index) { active_ = index; }
  vega::Settings load(uint8_t index) {
    const auto &asset = courseAsset(index);
    const vega::Settings base = defaults(index);
    const auto read_selected = [index](vega::Settings &s) { return readLegacy(index, s); };
    if (!general_loaded_) {
      vega::GeneralSettings stored;
      if (!readGeneral(stored)) {
        // Older firmware stored vehicle settings with the selected course.
        // A newly selected course has no blob yet; the old "vega" namespace
        // still carries the vehicle calibration and output polarity.
        stored = migratedGeneralSettings(base, read_selected,
                                         [](vega::Settings &s) { return loadFrom("vega", s); });
        if (saveGeneral(stored)) diagnostic("[SETTINGS] migrated general settings");
      }
      general_ = stored;
      general_loaded_ = true;
    }
    vega::CourseSettings course;
    if (!readCourse(index, course)) {
      course = migratedCourseSettings(base, read_selected);
      if (writeCourse(index, course)) diagnostic("[SETTINGS] migrated course=%s", asset.data->id);
    }
    vega::Settings s = base;
    vega::applyGeneral(s, general_);
    vega::applyCourse(s, course);
    return s;
  }
  bool saveGeneral(const vega::GeneralSettings &s) override {
    if (!vega::validGeneralSettings(s)) return false;
    Preferences p;
    if (!p.begin("vega-device", false)) return false;
    const bool ok = p.putBytes("settings", &s, sizeof(s)) == sizeof(s);
    p.end();
    if (ok) general_ = s;
    return ok;
  }
  bool saveCourse(const vega::CourseSettings &s) override { return writeCourse(active_, s); }
 private:
  uint8_t active_ = 0;
  bool general_loaded_ = false;
  vega::GeneralSettings general_{};
  static vega::Settings defaults(uint8_t index) {
    vega::Settings s;
    vega::applyCourse(s, courseAsset(index).defaults());
    return s;
  }
  static bool readGeneral(vega::GeneralSettings &s) {
    Preferences p;
    if (!p.begin("vega-device", true)) return false;
    vega::GeneralSettings candidate;
    const bool ok = p.getBytesLength("settings") == sizeof(candidate) &&
                    p.getBytes("settings", &candidate, sizeof(candidate)) == sizeof(candidate) &&
                    vega::validGeneralSettings(candidate);
    p.end();
    if (ok) s = candidate;
    return ok;
  }
  static bool readCourse(uint8_t index, vega::CourseSettings &s) {
    Preferences p;
    if (!p.begin(courseAsset(index).settings_namespace, true)) return false;
    vega::CourseSettings candidate;
    const bool ok = p.getBytesLength("course") == sizeof(candidate) &&
                    p.getBytes("course", &candidate, sizeof(candidate)) == sizeof(candidate) &&
                    vega::validCourseSettings(candidate);
    p.end();
    if (ok) s = candidate;
    return ok;
  }
  static bool writeCourse(uint8_t index, const vega::CourseSettings &s) {
    if (!vega::validCourseSettings(s)) return false;
    Preferences p;
    if (!p.begin(courseAsset(index).settings_namespace, false)) return false;
    const bool ok = p.putBytes("course", &s, sizeof(s)) == sizeof(s);
    p.end();
    return ok;
  }
  static bool readLegacy(uint8_t index, vega::Settings &s) {
    const auto &asset = courseAsset(index);
    if (loadFrom(asset.settings_namespace, s)) return true;
    if (!asset.legacy_settings_namespace || !loadFrom(asset.legacy_settings_namespace, s))
      return false;
    if (asset.legacy_total_target_s && s.total_target_s == asset.legacy_total_target_s)
      s.total_target_s = asset.defaults().total_target_s;
    return true;
  }
  static bool loadFrom(const char *name, vega::Settings &s) {
    nvs_handle_t handle;
    bool loaded = false;
    if (nvs_open(name, NVS_READONLY, &handle) == ESP_OK) {
      size_t size = 0;
      if (nvs_get_blob(handle, "settings", nullptr, &size) == ESP_OK &&
          size <= sizeof(vega::Settings)) {
        alignas(vega::Settings) uint8_t blob[sizeof(vega::Settings)]{};
        if (nvs_get_blob(handle, "settings", blob, &size) == ESP_OK)
          loaded = vega::decodeSettingsBlob(blob, size, s);
      }
      nvs_close(handle);
    }
    return loaded;
  }
} settings_store;

// SD I/O happens exclusively in the writer task. Queue overflow is surfaced as a warning.
enum class RecordKind : uint8_t { Begin, Sample, Event, End };
struct Record {
  RecordKind kind;
  vega::Snapshot data;
  vega::Event event{};
  vega::EndReason reason{};
  uint32_t losses_at_begin = 0;
};
void enqueueRecord(const Record &r) {
  if (xQueueSend(records, &r, 0) != pdTRUE) {
    record_loss_count.fetch_add(1, std::memory_order_relaxed);
    sd_error = true;
    sd_last_failure = SdFailure::QueueFull;
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
    r.data.course_index = active_course.load();
    r.data.lap_count = courseAsset(r.data.course_index).data->lap_count;
    r.losses_at_begin = record_loss_count.load(std::memory_order_relaxed);
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
  const bool high = gpio_get_level(static_cast<gpio_num_t>(kReed)) != 0;
  portENTER_CRITICAL_ISR(&wheel_lock);
  if (reed_filter.edge(high, now, kReedReleaseUs,
                       debounce_us.load(std::memory_order_relaxed))) {
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
    case vega::Event::ManualFinish:
      return "manual_finish";
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

bool readPlan(const char *name, uint8_t course_index, vega::Strategy &out,
              char *error, size_t error_size) {
  if (strstr(name, "..") || (strcmp(name, "strategy.json") != 0 &&
      strncmp(name, "strategies/", 11) != 0)) {
    snprintf(error, error_size, "invalid path");
    return false;
  }
  char path[112];
  if (snprintf(path, sizeof(path), "/sdcard/vega/%s", name) >= sizeof(path)) return false;
  FILE *file = fopen(path, "rb");
  if (!file) {
    snprintf(error, error_size, "file missing");
    return false;
  }
  char *contents = static_cast<char *>(malloc(vega::kMaxStrategyFileBytes + 1));
  bool ok = false;
  if (!contents) snprintf(error, error_size, "out of memory");
  else if (fseek(file, 0, SEEK_END) == 0) {
    const long length = ftell(file);
    if (length > 0 && length <= static_cast<long>(vega::kMaxStrategyFileBytes) &&
        fseek(file, 0, SEEK_SET) == 0 &&
        fread(contents, 1, length, file) == static_cast<size_t>(length)) {
      const auto &asset = courseAsset(course_index);
      vega::Course course(*asset.data);
      ok = vega::parseStrategy(contents, length, course, asset.data->id,
                               out, error, error_size);
    } else snprintf(error, error_size, "empty, too large, or unreadable");
  } else snprintf(error, error_size, "read failed");
  free(contents);
  fclose(file);
  return ok;
}

void scanPlans(const PlanRequest &request) {
  if (request.course_index != active_course.load()) return;
  // SD task is the sole caller; keep the bounded catalog off its 8 KB stack.
  static PlanChoices found;
  found = {};
  found.course_index = request.course_index;
  found.sd_available = sd_ready;
  found.count = 1;
  snprintf(found.items[0].label, sizeof(found.items[0].label), "NO STRATEGY");
  found.items[0].valid = true;
  auto add = [&](const char *name) {
    if (found.count >= kMaxPlanChoices) return;
    auto &item = found.items[found.count++];
    snprintf(item.filename, sizeof(item.filename), "%s", name);
    vega::Strategy parsed{};
    char reason[48]{};
    item.valid = readPlan(name, request.course_index, parsed, reason, sizeof(reason));
    if (item.valid) snprintf(item.label, sizeof(item.label), "%s", parsed.plan_id);
    else {
      snprintf(item.label, sizeof(item.label), "%s", name);
      snprintf(item.error, sizeof(item.error), "%s", reason);
    }
  };
  if (sd_ready) {
    if (SD_MMC.exists("/vega/strategy.json")) add("strategy.json");
    DIR *dir = opendir("/sdcard/vega/strategies");
    if (dir) {
      dirent *entry;
      while ((entry = readdir(dir)) != nullptr) {
        const size_t len = strlen(entry->d_name);
        if (len > 5 && strcmp(entry->d_name + len - 5, ".json") == 0 && len < 36) {
          char name[48];
          snprintf(name, sizeof(name), "strategies/%s", entry->d_name);
          add(name);
        }
      }
      closedir(dir);
    }
  }
  const char *desired = request.filename;
  if (!desired[0]) desired = "none";
  vega::PlanState next = vega::PlanState::Missing;
  if (strcmp(desired, "none") != 0) found.selected = 255;
  if (!sd_ready) snprintf(found.message, sizeof(found.message), "SD NOT AVAILABLE");
  else if (strcmp(desired, "none") != 0) {
    next = vega::PlanState::Invalid;
    snprintf(found.message, sizeof(found.message), "STRATEGY FILE MISSING");
    for (uint8_t i = 1; i < found.count; ++i) {
      if (strcmp(found.items[i].filename, desired) != 0) continue;
      if (!found.items[i].valid) {
        snprintf(found.message, sizeof(found.message), "INVALID: %s", found.items[i].error);
        break;
      }
      vega::Strategy parsed{};
      char reason[48]{};
      if (readPlan(desired, request.course_index, parsed, reason, sizeof(reason))) {
        xQueueOverwrite(strategies, &parsed);
        found.selected = i;
        next = vega::PlanState::Ready;
        found.message[0] = 0;
      } else snprintf(found.message, sizeof(found.message), "INVALID: %s", reason);
      break;
    }
  } else snprintf(found.message, sizeof(found.message), "NO STRATEGY SELECTED");
  if (request.course_index != active_course.load()) return;
  xQueueOverwrite(plan_choices, &found);
  plan_state = next;
  diagnostic("[PLAN] course=%s selected=%s state=%u %s", courseAsset(request.course_index).data->id,
             desired, static_cast<unsigned>(next), found.message);
}

void installPlan(PlanImport request) {
  vega::Snapshot current;
  if (!sd_ready || !snapshot(current) || current.race.phase != vega::RacePhase::Waiting ||
      current.course_index != active_course.load() ||
      SD_MMC.exists("/vega/strategy.json")) {
    Serial.println("[PLAN UPLOAD] rejected: SD unavailable, timing active, or file exists");
    free(request.contents);
    return;
  }
  vega::Strategy parsed;
  const auto &asset = courseAsset(active_course.load());
  vega::Course course(*asset.data);
  char error[80]{};
  if (!vega::parseStrategy(request.contents, request.length, course, asset.data->id,
                           parsed, error, sizeof(error))) {
    Serial.printf("[PLAN UPLOAD] rejected: %s\n", error);
    free(request.contents);
    return;
  }
  constexpr const char *temporary = "/sdcard/vega/strategy.json.tmp";
  constexpr const char *final = "/sdcard/vega/strategy.json";
  FILE *file = fopen(temporary, "wb");
  bool written = file && fwrite(request.contents, 1, request.length, file) == request.length;
  if (file) {
    written = written && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) written = false;
  }
  free(request.contents);
  vega::Snapshot latest;
  if (!snapshot(latest) || latest.race.phase != vega::RacePhase::Waiting ||
      latest.course_index != current.course_index || active_course.load() != current.course_index)
    written = false;
  if (!written || rename(temporary, final) != 0) {
    remove(temporary);
    Serial.println("[PLAN UPLOAD] rejected: SD write failed");
    return;
  }
  QueuedCommand activate{};
  activate.command.kind = CommandKind::UseUploadedStrategy;
  activate.command.choice = current.course_index;
  activate.power_epoch = power_epoch.load();
  if (xQueueSend(commands, &activate, 0) != pdTRUE)
    Serial.println("[PLAN UPLOAD] saved; select from Waiting menu");
  else Serial.printf("[PLAN UPLOAD] installed id=%s\n", parsed.plan_id);
}

void sdTask(void *) {
  SD_MMC.setPins(43, 44, 39, 40, 41, 42);
  auto mount = []() {
    bool ready = SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_DEFAULT);
    if (ready && !SD_MMC.exists("/vega")) ready = SD_MMC.mkdir("/vega");
    sd_ready = ready;
    return ready;
  };
  mount();
  sd_error = !sd_ready || record_loss_count.load(std::memory_order_relaxed) != 0;
  Serial.printf("[SD] mount=%s card_bytes=%llu\n", sd_ready ? "OK" : "FAILED",
                sd_ready ? SD_MMC.cardSize() : 0ULL);
  plan_state = vega::PlanState::Loading;
  SdFile log;
  char last_path[80]{};
  uint64_t last_flush = 0;
  bool active = false, failed_session = false;
  for (;;) {
    PlanImport imported{};
    if (xQueueReceive(plan_imports, &imported, 0) == pdTRUE) installPlan(imported);
    PlanRequest plan_request{};
    if (xQueueReceive(plan_requests, &plan_request, 0) == pdTRUE) {
      if (!sd_ready) {
        SD_MMC.end();
        if (mount()) Serial.println("[SD] mount recovered for strategy scan");
      }
      scanPlans(plan_request);
    }
    Record r{};
    if (xQueueReceive(records, &r, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (r.kind == RecordKind::Begin) {
        if (log) log.close();
        active = true;
        failed_session = false;
        if (!sd_ready) {
          SD_MMC.end();
          if (mount()) Serial.println("[SD] mount recovered");
        }
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
          sd_last_failure = SdFailure::Open;
          Serial.println("[SD] cannot open session");
        } else {
          // Metadata is a distinct line; SD samples extend the MQTT fields with GPS diagnostics.
          char meta[1100];
          const auto &s = r.data.settings;
          int n = snprintf(
              meta, sizeof(meta),
              "{\"type\":\"session\",\"schema_version\":3,\"boot_session\":%lu,\"started_uptime_"
              "ms\":%llu,\"course_id\":\"%s\",\"lap_count\":%u,\"total_target_s\":%lu,\"lap_target_s\":[%lu,%lu,%lu,%lu,%lu,%lu,%lu],"
              "\"start\":[%.8f,%.8f],\"timing\":[%.8f,%.8f],\"goal\":[%.8f,%.8f]}\n",
              static_cast<unsigned long>(r.data.race.session), r.data.now_ms,
              courseAsset(r.data.course_index).data->id, r.data.lap_count,
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
              log.write(reinterpret_cast<const uint8_t *>(meta), n) != static_cast<size_t>(n) ||
              !log.flush()) {
            failed_session = true;
            sd_last_failure = SdFailure::Metadata;
            Serial.println("[SD] metadata write or flush failed");
          } else if (record_loss_count.load(std::memory_order_relaxed) == r.losses_at_begin) {
            // A successfully opened new session clears errors from the prior one.
            sd_error = false;
            if (record_loss_count.load(std::memory_order_relaxed) != r.losses_at_begin)
              sd_error = true;
          }
          Serial.printf("[SD] session opened %s\n", last_path);
        }
      } else if (active && log && !failed_session) {
        char line[1100]{};
        size_t n = 0;
        if (r.kind == RecordKind::Sample)
          n = vega::sessionSampleJson(r.data, line, sizeof(line), network_config::machine_id,
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
        const size_t written = n ? log.write(reinterpret_cast<const uint8_t *>(line), n) : 0;
        const size_t newline = written == n && n ? log.write('\n') : 0;
        if (!n || written != n || newline != 1) {
          failed_session = true;
          sd_last_failure = SdFailure::Write;
          Serial.printf("[SD] write failed kind=%u bytes=%u/%u newline=%u\n",
                        static_cast<unsigned>(r.kind), static_cast<unsigned>(written),
                        static_cast<unsigned>(n), static_cast<unsigned>(newline));
        }
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
            sd_last_failure = SdFailure::EndFlush;
            Serial.println("[SD] end flush failed");
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
        sd_last_failure = SdFailure::Flush;
        log.close();
        Serial.println("[SD] flush failed; timing continues");
      }
      last_flush = now;
    }
    if (readback.exchange(false)) {
      const uint32_t requested_session = readback_session.exchange(0);
      if (active) {
        Serial.println("[SD READBACK] cancel or finish timing before reading logs");
        continue;
      }
      if (log && !log.flush()) {
        sd_error = true;
        failed_session = true;
        sd_last_failure = SdFailure::ReadbackFlush;
        log.close();
      }
      char requested_path[80]{};
      if (requested_session)
        snprintf(requested_path, sizeof(requested_path), "/vega/session-%010lu.jsonl",
                 static_cast<unsigned long>(requested_session));
      else if (sd_ready && (!last_path[0] || !SD_MMC.exists(last_path))) {
        // USB serial access can reboot Tab5; recover the most recent file from
        // the persisted session counter so `log` still works after a field run.
        Preferences p;
        uint32_t number = 0;
        if (p.begin("vega-log", true)) {
          number = p.getUInt("next", 0);
          p.end();
        }
        for (unsigned attempts = 0; number && attempts < 100; --number, ++attempts) {
          snprintf(last_path, sizeof(last_path), "/vega/session-%010lu.jsonl",
                   static_cast<unsigned long>(number));
          if (SD_MMC.exists(last_path)) break;
          last_path[0] = '\0';
        }
      }
      const char *path = requested_session ? requested_path : last_path;
      SdFile file = sd_ready && path[0] ? SdFile(path, "r") : SdFile();
      if (!file)
        Serial.println("[SD READBACK] no readable session");
      else {
        char header[140];
        int count = snprintf(header, sizeof(header), "[SD READBACK BEGIN] %s bytes=%llu\n",
                             path, static_cast<uint64_t>(file.size()));
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
  constexpr vega::Millis kWifiRetryMs = 10000;
  const bool wifi_configured = network_config::ssid[0];
  const bool mqtt_configured = VEGA_ENABLE_MQTT && network_config::root_ca[0] &&
                               network_config::client_cert[0] && network_config::client_key[0];
  esp_mqtt_client_handle_t client = nullptr;
  bool mqtt_started = false;
  vega::Millis last_wifi_attempt = clock_source.now();
  if (wifi_configured) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(network_config::ssid, network_config::password);
    sntp_set_time_sync_notification_cb(ntpSynced);
    configTime(9 * 3600, 0, network_config::ntp_server);
    if (mqtt_configured) Serial.println("[MQTT] waiting for Wi-Fi IP");
    else Serial.println("[MQTT] disabled in this build");
  } else
    Serial.println("[NET] Wi-Fi not configured; MQTT/NTP offline");
  for (;;) {
    // Arduino marks WL_CONNECTED only after the STA_GOT_IP event.
    const bool wifi_ready = wifi_configured && WiFi.status() == WL_CONNECTED;
    const auto now = clock_source.now();
    if (wifi_ready)
      last_wifi_attempt = now;
    else if (wifi_configured && now - last_wifi_attempt >= kWifiRetryMs) {
      last_wifi_attempt = now;
      const auto before = WiFi.status();
      const auto result = WiFi.begin(network_config::ssid, network_config::password);
      diagnostic("[NET] Wi-Fi retry previous=%u begin=%u", static_cast<unsigned>(before),
                 static_cast<unsigned>(result));
    }
    if (!wifi_ready && mqtt_started) {
      mqtt_connected = false;
      esp_mqtt_client_stop(client);
      mqtt_started = false;
      Serial.println("[MQTT] stopped; Wi-Fi IP unavailable");
    } else if (wifi_ready && mqtt_configured && !mqtt_started) {
      if (!client) {
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
        if (client) esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqttEvent, nullptr);
      }
      if (client && esp_mqtt_client_start(client) == ESP_OK) {
        mqtt_started = true;
        Serial.println("[MQTT] starting after Wi-Fi got IP");
      }
    }
    vega::Snapshot s{};
    if (xQueueReceive(transmissions, &s, pdMS_TO_TICKS(500)) == pdTRUE && client &&
        mqtt_connected) {
      char payload[1100];
      auto n = vega::telemetryJson(s, payload, sizeof(payload), network_config::machine_id,
                                   network_config::memo);
      if (n && clock_source.now() - s.now_ms < 1500) {
        int message_id = esp_mqtt_client_publish(client, network_config::topic, payload, n, 0, 0);
        if (message_id < 0) diagnostic("[MQTT] publish failed code=%d", message_id);
      }
    }
  }
}

void applicationTask(void *) {
  const uint8_t initial_index = savedCourseIndex();
  active_course = initial_index;
  settings_store.select(initial_index);
  vega::Course course(*courseAsset(initial_index).data);
  auto settings = settings_store.load(initial_index);
  Serial.printf("[COURSE] id=%s length_m=%.1f start=%.6f,%.6f lap=%.6f,%.6f "
                "goal=%.6f,%.6f\n", courseAsset(initial_index).data->id, course.length(),
                settings.start.latitude, settings.start.longitude,
                settings.timing.latitude, settings.timing.longitude,
                settings.goal.latitude, settings.goal.longitude);
  debounce_us = settings.pulse_debounce_us;
  vega::Application app(clock_source, engine_output, settings_store, recorder, telemetry, course,
                        settings);
  app.selectCourse(course, initial_index, settings);
  PlanRequest initial_plan{};
  initial_plan.course_index = initial_index;
  savedPlanName(initial_plan.filename, sizeof(initial_plan.filename));
  xQueueOverwrite(plan_requests, &initial_plan);
  HardwareSerial gps_uart(1);
  gps_uart.setRxBufferSize(2048);
  uint8_t gps_config_step = 2;
  vega::Millis gps_config_sent_ms = 0, gps_rate_window_ms = 0;
  uint32_t gps_rate_window_rmc = 0;
  uint8_t casic_frame[64]{};
  size_t casic_used = 0;
  bool nav_query_pending = false;
  bool walk_ack_pending = false;
  vega::Millis nav_query_deadline = 0;
  auto openGps = [&](vega::GpsSource source) {
    gps_uart.end();
    casic_used = 0;
    nav_query_pending = false;
    walk_ack_pending = false;
    gps_nav_query_requested = false;
    gps_walk_requested = false;
    gps_bytes = 0;
    gps_rmc = 0;
    gps_rmc_hz_x10 = 0;
    const bool port_a = source == vega::GpsSource::PortA;
    if (port_a && courseAsset(active_course.load()).pedestrian_gps)
      gps_walk_requested = true;
    const int rx = port_a ? kGpsPortARx : kGpsBusRx;
    const int tx = port_a ? kGpsPortATx : kGpsBusTx;
    gps_uart.begin(9600, SERIAL_8N1, rx, tx);
    gps_config_step = port_a ? 0 : 2;
    gps_rate_window_ms = clock_source.now();
    gps_rate_window_rmc = 0;
    diagnostic("[GPS] source=%s rx=%d tx=%d baud=9600", port_a ? "PORT_A" : "M5BUS", rx, tx);
  };
  openGps(settings.gps_source);
  pinMode(kReed, INPUT_PULLUP);
  portENTER_CRITICAL(&wheel_lock);
  reed_filter.reset(digitalRead(kReed) != LOW, esp_timer_get_time());
  portEXIT_CRITICAL(&wheel_lock);
  attachInterrupt(digitalPinToInterrupt(kReed), reedInterrupt, CHANGE);
  vega::NmeaParser parser;
  diagnostic("[APP] ready; electrical OFF; commands: status/start/cancel/lap/on/off/ignite/log");
  for (;;) {
    if (urgent_off.exchange(false)) {
      app.power(false);
      diagnostic("[APP] electrical OFF; old ON/IGNITE commands invalidated");
    }
    vega::WheelInput input;
    portENTER_CRITICAL(&wheel_lock);
    input = wheel_input;
    portEXIT_CRITICAL(&wheel_lock);
    app.wheel(input);
    for (unsigned budget = 0; budget < 512 && gps_uart.available(); ++budget) {
      vega::GpsFix fix;
      const int byte = gps_uart.read();
      if (byte < 0) break;
      const char c = static_cast<char>(byte);
      const auto received_ms = clock_source.now();
      gps_bytes.fetch_add(1, std::memory_order_relaxed);
      // A CFG-NAVX response shares the UART with NMEA. Consume only framed
      // binary packets while a query is pending; leave NMEA untouched.
      if (nav_query_pending && (casic_used || byte == 0xBA)) {
        if (casic_used == 0) {
          casic_frame[casic_used++] = static_cast<uint8_t>(byte);
          continue;
        }
        if (casic_used == 1 && byte != 0xCE) {
          casic_used = 0;
        } else {
          casic_frame[casic_used++] = static_cast<uint8_t>(byte);
          if (casic_used >= 6) {
            const size_t length = size_t(casic_frame[2]) | (size_t(casic_frame[3]) << 8);
            if (length % 4 || length + 10 > sizeof(casic_frame)) {
              casic_used = 0;
            } else if (casic_used == length + 10) {
              const uint8_t cls = casic_frame[4], id = casic_frame[5];
              uint32_t checksum = (uint32_t(id) << 24) | (uint32_t(cls) << 16) | length;
              for (size_t i = 0; i < length; i += 4)
                checksum += casicWord(casic_frame + 6 + i);
              if (checksum == casicWord(casic_frame + 6 + length)) {
                if (cls == 0x06 && id == 0x07 && length == 44) {
                  char packet_hex[sizeof(casic_frame) * 2 + 1]{};
                  for (size_t i = 0; i < length + 10; ++i)
                    snprintf(packet_hex + 2 * i, 3, "%02X", casic_frame[i]);
                  diagnostic("[GPS NAV] packet=%s", packet_hex);
                  float static_hold_mps;
                  memcpy(&static_hold_mps, casic_frame + 6 + 40, sizeof(static_hold_mps));
                  diagnostic("[GPS NAV] mode=%u fix_mode=%u static_hold_mps=%.3f mask=0x%08lX",
                             casic_frame[6 + 4], casic_frame[6 + 5], static_hold_mps,
                             static_cast<unsigned long>(casicWord(casic_frame + 6)));
                  nav_query_pending = false;
                } else if (cls == 0x05) {
                  diagnostic("[GPS NAV] ack_id=%u for_class=%u for_id=%u", id,
                             casic_frame[6], casic_frame[7]);
                  if (walk_ack_pending && casic_frame[6] == 0x06 && casic_frame[7] == 0x07) {
                    walk_ack_pending = false;
                    nav_query_pending = false;
                    if (id == 0x01) gps_nav_query_requested = true;
                  } else if (id == 0x00) {
                    nav_query_pending = false;
                  }
                }
              } else {
                diagnostic("[GPS NAV] bad CASIC checksum class=%u id=%u", cls, id);
              }
              casic_used = 0;
            }
          }
          continue;
        }
      }
      if (parser.feed(c, received_ms, fix)) {
        gps_rmc.fetch_add(1, std::memory_order_relaxed);
        app.gps(fix);
      }
    }
    const auto gps_now = clock_source.now();
    if (gps_config_step == 0 && gps_rmc.load(std::memory_order_relaxed) > 0) {
      // At 9600 bps, output RMC on every 200 ms fix and GGA once per second;
      // suppress unused sentences before increasing the fix rate.
      if (sendCasic(gps_uart, "PCAS03,5,0,0,0,1,0,0,0,0,0,,,0,0,,,,0")) {
        gps_config_step = 1;
        gps_config_sent_ms = gps_now;
      }
    } else if (gps_config_step == 1 && gps_now - gps_config_sent_ms >= 250) {
      if (sendCasic(gps_uart, "PCAS02,200")) {
        gps_config_step = 2;
        diagnostic("[GPS] PORT_A requested RMC 5 Hz / GGA 1 Hz");
      }
    }
    if (nav_query_pending && gps_now >= nav_query_deadline) {
      nav_query_pending = false;
      walk_ack_pending = false;
      casic_used = 0;
      diagnostic("[GPS NAV] response timeout");
    }
    if (app.snapshot().settings.gps_source == vega::GpsSource::PortA && gps_config_step == 2 &&
        !nav_query_pending && gps_walk_requested.exchange(false)) {
      walk_ack_pending = requestCasicWalking(gps_uart);
      nav_query_pending = walk_ack_pending;
      nav_query_deadline = gps_now + 3000;
      casic_used = 0;
      diagnostic("[GPS NAV] walking mode request %s (RAM only)",
                 walk_ack_pending ? "sent" : "failed");
    }
    if (app.snapshot().settings.gps_source == vega::GpsSource::PortA && gps_config_step == 2 &&
        !nav_query_pending && gps_nav_query_requested.exchange(false)) {
      nav_query_pending = requestCasicNavx(gps_uart);
      nav_query_deadline = gps_now + 3000;
      casic_used = 0;
      diagnostic("[GPS NAV] query %s", nav_query_pending ? "sent" : "failed");
    }
    if (gps_now - gps_rate_window_ms >= 5000) {
      const uint32_t count = gps_rmc.load(std::memory_order_relaxed);
      gps_rmc_hz_x10 = static_cast<uint32_t>(
          uint64_t(count - gps_rate_window_rmc) * 10000 / (gps_now - gps_rate_window_ms));
      gps_rate_window_rmc = count;
      gps_rate_window_ms = gps_now;
    }
    QueuedCommand queued{};
    for (unsigned budget = 0; budget < 8 && xQueueReceive(commands, &queued, 0) == pdTRUE;
         ++budget) {
      const auto &command = queued.command;
      if ((command.kind == CommandKind::PowerOn || command.kind == CommandKind::Ignite) &&
          queued.power_epoch != power_epoch.load(std::memory_order_acquire)) {
        diagnostic("[APP] old power command=%u skipped", static_cast<unsigned>(command.kind));
        continue;
      }
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
        case CommandKind::Finish:
          ok = app.manualFinish();
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
        case CommandKind::Configure: {
          const auto old_gps_source = app.snapshot().settings.gps_source;
          ok = app.configure(command.settings, command.scope);
          if (ok) {
            debounce_us = command.settings.pulse_debounce_us;
            if (command.settings.gps_source != old_gps_source) {
              gps_uart.flush();
              openGps(command.settings.gps_source);
              parser = vega::NmeaParser{};
            }
          }
          break;
        }
        case CommandKind::SelectCourse: {
          const uint8_t index = command.choice;
          const auto current = app.snapshot();
          ok = index < courseCount() && current.race.phase == vega::RacePhase::Waiting &&
               current.engine == vega::EnginePhase::Off;
          if (ok && index != current.course_index) {
            const auto replacement = settings_store.load(index);
            ok = vega::validSettings(replacement) &&
                 saveSelection(courseAsset(index).data->id, "none");
            if (ok) {
              course = vega::Course(*courseAsset(index).data);
              ok = app.selectCourse(course, index, replacement);
            }
            if (ok) {
              settings_store.select(index);
              active_course = index;
              debounce_us = replacement.pulse_debounce_us;
              parser = vega::NmeaParser{};
              openGps(replacement.gps_source);
              xQueueReset(strategies);
              plan_state = vega::PlanState::Loading;
              PlanRequest request{};
              request.course_index = index;
              snprintf(request.filename, sizeof(request.filename), "none");
              xQueueOverwrite(plan_requests, &request);
            }
          }
          break;
        }
        case CommandKind::SelectStrategy: {
          const auto current = app.snapshot();
          static PlanChoices choices{};
          ok = current.race.phase == vega::RacePhase::Waiting &&
               current.engine == vega::EnginePhase::Off &&
               xQueuePeek(plan_choices, &choices, 0) == pdTRUE &&
               choices.course_index == current.course_index &&
               command.choice < choices.count && choices.items[command.choice].valid;
          if (ok) {
            const char *name = command.choice == 0 ? "none" :
                               choices.items[command.choice].filename;
            ok = saveSelection(courseAsset(current.course_index).data->id, name);
            if (ok) {
              xQueueReset(strategies);
              plan_state = vega::PlanState::Loading;
              PlanRequest request{};
              request.course_index = current.course_index;
              snprintf(request.filename, sizeof(request.filename), "%s", name);
              xQueueOverwrite(plan_requests, &request);
            }
          }
          break;
        }
        case CommandKind::RefreshStrategies: {
          const auto current = app.snapshot();
          ok = current.race.phase == vega::RacePhase::Waiting &&
               current.engine == vega::EnginePhase::Off;
          if (ok) {
            PlanRequest request{};
            request.course_index = current.course_index;
            savedPlanName(request.filename, sizeof(request.filename));
            xQueueReset(strategies);
            plan_state = vega::PlanState::Loading;
            xQueueOverwrite(plan_requests, &request);
          }
          break;
        }
        case CommandKind::UseUploadedStrategy: {
          const auto current = app.snapshot();
          ok = current.race.phase == vega::RacePhase::Waiting &&
               current.engine == vega::EnginePhase::Off &&
               command.choice == current.course_index &&
               saveSelection(courseAsset(current.course_index).data->id, "strategy.json");
          if (ok) {
            xQueueReset(strategies);
            plan_state = vega::PlanState::Loading;
            PlanRequest request{};
            request.course_index = current.course_index;
            snprintf(request.filename, sizeof(request.filename), "strategy.json");
            xQueueOverwrite(plan_requests, &request);
          }
          break;
        }
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
  if (VEGA_ENABLE_MQTT &&
      mbedtls_platform_set_calloc_free(tlsPsramCalloc, tlsPsramFree) != 0) {
    Serial.println("[MQTT] unable to route TLS allocations to PSRAM");
    return false;
  }
  commands = xQueueCreate(12, sizeof(QueuedCommand));
  snapshots = xQueueCreate(1, sizeof(vega::Snapshot));
  records = xQueueCreate(48, sizeof(Record));
  transmissions = xQueueCreate(1, sizeof(vega::Snapshot));
  diagnostics = xQueueCreate(16, sizeof(Diagnostic));
  strategies = xQueueCreate(1, sizeof(vega::Strategy));
  plan_imports = xQueueCreate(1, sizeof(PlanImport));
  plan_requests = xQueueCreate(1, sizeof(PlanRequest));
  plan_choices = xQueueCreate(1, sizeof(PlanChoices));
  if (!commands || !snapshots || !records || !transmissions || !diagnostics || !strategies ||
      !plan_imports || !plan_requests || !plan_choices)
    return false;
  if (xTaskCreate(diagnosticTask, "vega_log", 3072, nullptr, 1, nullptr) != pdPASS ||
      xTaskCreate(sdTask, "vega_sd", 8192, nullptr, 1, nullptr) != pdPASS ||
      xTaskCreate(networkTask, "vega_net", 8192, nullptr, 1, nullptr) != pdPASS ||
      xTaskCreate(applicationTask, "vega_app", 8192, nullptr, 4, nullptr) != pdPASS)
    return false;
  return true;
}
bool prepareOutputs() {
  const auto index = savedCourseIndex();
  settings_store.select(index);
  auto settings = settings_store.load(index);
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
    power_epoch.fetch_add(1, std::memory_order_acq_rel);
    urgent_off.store(true, std::memory_order_release);
    return true;
  }
  QueuedCommand queued{c, power_epoch.load(std::memory_order_acquire)};
  return xQueueSend(commands, &queued, 0) == pdTRUE;
}
bool snapshot(vega::Snapshot &s) { return snapshots && xQueuePeek(snapshots, &s, 0) == pdTRUE; }
bool strategy(vega::Strategy &s) {
  return strategies && xQueuePeek(strategies, &s, 0) == pdTRUE;
}
bool planChoices(PlanChoices &out) {
  return plan_choices && xQueuePeek(plan_choices, &out, 0) == pdTRUE;
}
vega::UiStatus status() {
  vega::UiStatus s;
  s.sd_ready = sd_ready;
  s.sd_error = sd_error;
  s.plan_state = plan_state.load();
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
void requestLogReadback(uint32_t session) {
  readback_session = session;
  readback = true;
}
void serialPoll() {
  static char buffer[64];
  static size_t count = 0;
  static char *incoming_plan = nullptr;
  static size_t incoming_expected = 0, incoming_received = 0;
  static uint32_t incoming_last_ms = 0;
  if (incoming_plan && millis() - incoming_last_ms > 15000) {
    Serial.printf("[PLAN UPLOAD] timeout received=%u expected=%u\n",
                  static_cast<unsigned>(incoming_received),
                  static_cast<unsigned>(incoming_expected));
    free(incoming_plan);
    incoming_plan = nullptr;
    incoming_expected = incoming_received = 0;
  }
  for (unsigned budget = 0; budget < 128 && Serial.available(); ++budget) {
    char c = Serial.read();
    if (incoming_plan) {
      incoming_plan[incoming_received++] = c;
      incoming_last_ms = millis();
      if (incoming_received == incoming_expected) {
        PlanImport request{incoming_plan, incoming_expected};
        if (xQueueSend(plan_imports, &request, 0) == pdTRUE)
          Serial.println("[PLAN UPLOAD] queued");
        else {
          free(incoming_plan);
          Serial.println("[PLAN UPLOAD] queue full");
        }
        incoming_plan = nullptr;
        incoming_expected = incoming_received = 0;
      }
      continue;
    }
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
              "[STATUS] phase=%u lap=%u course=%s lap_count=%u total_ms=%llu "
              "power_phase=%u power_pin=%d ignition_pin=%d "
              "gps=%u gps_source=%s gps_bytes=%lu gps_rmc=%lu gps_rmc_hz=%lu.%lu pulses=%llu "
              "sd_ready=%u sd_error=%u sd_last_failure=%s sd_record_lost=%lu "
              "wifi=%u mqtt=%u ntp=%u total_target_s=%lu "
              "lap1_target_s=%lu brightness=%lu\n",
              static_cast<unsigned>(s.race.phase), s.race.lap,
              courseAsset(s.course_index).data->id, s.lap_count, s.race.total_ms,
              static_cast<unsigned>(s.engine), gpio_get_level(kPower), gpio_get_level(kIgnition),
              s.gps_fresh, s.settings.gps_source == vega::GpsSource::PortA ? "PORT_A" : "M5BUS",
              static_cast<unsigned long>(gps_bytes.load(std::memory_order_relaxed)),
              static_cast<unsigned long>(gps_rmc.load(std::memory_order_relaxed)),
              static_cast<unsigned long>(gps_rmc_hz_x10.load(std::memory_order_relaxed) / 10),
              static_cast<unsigned long>(gps_rmc_hz_x10.load(std::memory_order_relaxed) % 10),
              s.wheel.pulses, st.sd_ready, st.sd_error,
              sdFailureText(sd_last_failure.load(std::memory_order_relaxed)),
              static_cast<unsigned long>(record_loss_count.load(std::memory_order_relaxed)),
              WiFi.status() == WL_CONNECTED, st.mqtt_connected,
              st.time_valid, static_cast<unsigned long>(s.settings.total_target_s),
              static_cast<unsigned long>(s.settings.lap_target_s[0]),
              static_cast<unsigned long>(s.settings.display_brightness));
      } else if (!strcmp(buffer, "wheel-debug")) {
        uint64_t raw, rejected_release, rejected_interval, accepted;
        portENTER_CRITICAL(&wheel_lock);
        raw = reed_filter.rawFalls();
        rejected_release = reed_filter.rejectedRelease();
        rejected_interval = reed_filter.rejectedInterval();
        accepted = wheel_input.pulses;
        portEXIT_CRITICAL(&wheel_lock);
        Serial.printf("[WHEEL] raw_falls=%llu accepted=%llu rejected_release=%llu "
                      "rejected_interval=%llu level=%d release_us=%lu debounce_us=%lu\n",
                      raw, accepted, rejected_release, rejected_interval, digitalRead(kReed),
                      static_cast<unsigned long>(kReedReleaseUs),
                      static_cast<unsigned long>(debounce_us.load(std::memory_order_relaxed)));
      } else if (!strcmp(buffer, "plan-status")) {
        vega::Strategy current;
        static PlanChoices choices{};
        const bool loaded = plan_state.load() == vega::PlanState::Ready && strategy(current);
        const bool have_choices = planChoices(choices);
        Serial.printf("[PLAN] state=%u loaded=%u id=%s selected=%u choices=%u reason=%s\n",
                      static_cast<unsigned>(plan_state.load()), loaded,
                      loaded ? current.plan_id : "-", have_choices ? choices.selected : 255,
                      have_choices ? choices.count : 0,
                      have_choices ? choices.message : "not scanned");
      } else if (!strncmp(buffer, "plan-upload ", 12)) {
        char *end = nullptr;
        unsigned long length = strtoul(buffer + 12, &end, 10);
        vega::Snapshot current;
        if (!end || *end || length == 0 || length > vega::kMaxStrategyFileBytes ||
            !sd_ready || !snapshot(current) ||
            current.race.phase != vega::RacePhase::Waiting) {
          Serial.println("[PLAN UPLOAD] rejected: invalid size, SD unavailable, timing active, or file exists");
        } else {
          incoming_plan = static_cast<char *>(malloc(length));
          if (incoming_plan) {
            incoming_expected = length;
            incoming_received = 0;
            incoming_last_ms = millis();
            Serial.printf("[PLAN UPLOAD] ready bytes=%lu\n", length);
          } else Serial.println("[PLAN UPLOAD] allocation failed");
        }
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
      } else if (!strcmp(buffer, "gps-nav")) {
        vega::Snapshot s;
        if (snapshot(s) && s.settings.gps_source == vega::GpsSource::PortA &&
            s.race.phase == vega::RacePhase::Waiting) {
          gps_nav_query_requested = true;
          Serial.println("[GPS NAV] query queued");
        } else {
          Serial.println("[GPS NAV] available only with Port.A GPS before timing");
        }
      } else if (!strcmp(buffer, "gps-walk")) {
        vega::Snapshot s;
        if (snapshot(s) && s.settings.gps_source == vega::GpsSource::PortA &&
            s.race.phase == vega::RacePhase::Waiting) {
          gps_walk_requested = true;
          Serial.println("[GPS NAV] walking mode queued (RAM only)");
        } else {
          Serial.println("[GPS NAV] available only with Port.A GPS before timing");
        }
      } else if (!strncmp(buffer, "config ", 7)) {
        char key[32], value[24];
        vega::Snapshot s;
        if (sscanf(buffer + 7, "%31s %23s", key, value) == 2 && snapshot(s)) {
          Command c{};
          c.kind = CommandKind::Configure;
          c.settings = s.settings;
          c.scope = vega::SettingsScope::General;
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
          else if (!strcmp(key, "course_corridor_m")) {
            c.scope = vega::SettingsScope::Course;
            c.settings.course_corridor_m = number;
          }
          else if (!strcmp(key, "max_gps_step_m"))
            c.settings.max_gps_step_m = number;
          else if (!strcmp(key, "min_lap_progress_m")) {
            c.scope = vega::SettingsScope::Course;
            c.settings.min_lap_progress_m = number;
          }
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
          else if (!strcmp(key, "min_lap_ms")) {
            c.scope = vega::SettingsScope::Course;
            c.settings.min_lap_ms = number;
          } else if (!strcmp(key, "lap_duplicate_ms")) {
            c.scope = vega::SettingsScope::Course;
            c.settings.lap_duplicate_ms = number;
          }
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
      else if (!strncmp(buffer, "log-read ", 9)) {
        char *end = nullptr;
        const unsigned long long session = strtoull(buffer + 9, &end, 10);
        if (end != buffer + 9 && *end == '\0' && session > 0 && session <= UINT32_MAX)
          requestLogReadback(static_cast<uint32_t>(session));
        else
          Serial.println("[SD READBACK] use log-read SESSION_NUMBER");
      }
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
