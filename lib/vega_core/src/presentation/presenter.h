#pragma once
#include "domain/types.h"
#include "ports/ports.h"

namespace vega {
struct UiStatus {
  bool sd_ready = false, sd_error = false, mqtt_connected = false, network_configured = false;
  bool time_valid = false, ntp_holdover = false;
  char clock[24] = "--:--:-- JST";
};
struct DisplayModel {
  RacePhase phase = RacePhase::Waiting;
  char speed[16]{}, average[16]{}, lap[24]{}, total[24]{}, lap_time[24]{}, total_target[40]{},
      lap_target[40]{};
  char notice[100]{}, map_status[48]{}, gps_status[32]{}, link[32]{}, race_status[32]{},
      action[48]{}, detail[60]{};
  char clock[24]{}, ntp[24]{};
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
  explicit Presenter(IView &view) : view_(view) {}
  Presenter(IView &view, ICommandSink &commands) : view_(view), commands_(&commands) {}
  void render(const Snapshot &snapshot, const UiStatus &status);
  bool request(const Command &command);
  const Settings &settings() const { return settings_; }
  RacePhase phase() const { return phase_; }
  static void formatTime(uint64_t ms, char *text, size_t capacity);

 private:
  IView &view_;
  ICommandSink *commands_ = nullptr;
  Settings settings_{};
  RacePhase phase_ = RacePhase::Waiting;
};
}  // namespace vega
