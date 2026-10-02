#pragma once
#include "domain/types.h"
#include "domain/strategy.h"
#include "ports/ports.h"

namespace vega {
enum class PlanState : uint8_t { Loading, Missing, Invalid, Ready };
struct UiStatus {
  bool sd_ready = false, sd_error = false, mqtt_connected = false, network_configured = false;
  bool time_valid = false, ntp_holdover = false;
  PlanState plan_state = PlanState::Loading;
  char clock[24] = "--:--:-- JST";
};
struct DisplayModel {
  RacePhase phase = RacePhase::Waiting;
  char speed[16]{}, average[16]{}, lap[24]{}, total[24]{}, lap_time[24]{}, total_target[40]{},
      lap_target[40]{};
  char notice[100]{}, map_status[48]{}, gps_status[32]{}, link[32]{}, race_status[32]{},
      action[48]{}, detail[60]{};
  char clock[24]{}, ntp[24]{};
  char gps_latitude[32]{}, gps_longitude[32]{};
  char plan_status[32]{};
  StrategyLap plan_lap{};
  bool plan_loaded = false;
  uint8_t plan_lap_number = 0;
  uint16_t ignition_prepare_permille = 0;
  uint8_t display_brightness = kDefaultDisplayBrightness;
  bool ignition_preparing = false;
  bool power_on = false, ignition_enabled = false, lap_enabled = false, finish_mode = false,
       heartbeat = false,
       pulse = false, gps_ok = false;
  bool position_visible = false, position_stale = false, overtime = false;
  int marker_x = 0, marker_y = 0;
  double marker_heading = 0;
};
struct IView {
  virtual ~IView() = default;
  virtual void show(const DisplayModel &model) = 0;
};
class Presenter {
 public:
  explicit Presenter(IView &view, uint8_t lap_count = kLapCount)
      : view_(view), lap_count_(lap_count) {}
  Presenter(IView &view, ICommandSink &commands, uint8_t lap_count = kLapCount)
      : view_(view), commands_(&commands), lap_count_(lap_count) {}
  void render(const Snapshot &snapshot, const UiStatus &status,
              const Strategy *strategy = nullptr);
  bool request(const Command &command);
  const Settings &settings() const { return settings_; }
  RacePhase phase() const { return phase_; }
  void setLapCount(uint8_t count) { if (count >= 2 && count <= kLapCount) lap_count_ = count; }
  static void formatTime(uint64_t ms, char *text, size_t capacity);

 private:
  IView &view_;
  ICommandSink *commands_ = nullptr;
  uint8_t lap_count_ = kLapCount;
  Settings settings_{};
  RacePhase phase_ = RacePhase::Waiting;
};
}  // namespace vega
