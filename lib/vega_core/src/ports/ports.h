#pragma once
#include "domain/types.h"
namespace vega {
enum class CommandKind : uint8_t { Start, Cancel, Lap, PowerOn, PowerOff, Ignite, Configure, Finish,
                                   SelectCourse, SelectStrategy, RefreshStrategies,
                                   UseUploadedStrategy };
struct Command {
  CommandKind kind;
  Settings settings{};
  uint8_t choice = 0;
};
struct ICommandSink {
  virtual ~ICommandSink() = default;
  virtual bool submit(const Command &command) = 0;
};
struct IClock {
  virtual ~IClock() = default;
  virtual Millis now() const = 0;
  virtual uint64_t nowMicros() const { return now() * 1000; }
};
struct IEngineOutput {
  virtual ~IEngineOutput() = default;
  virtual void setPower(bool on) = 0;
  virtual bool pulse(uint32_t duration_ms) = 0;
  virtual void stopPulse() = 0;
  virtual void configurePower(bool active_high) = 0;
};
struct ISettingsStore {
  virtual ~ISettingsStore() = default;
  virtual bool save(const Settings &s) = 0;
};
struct ISessionRecorder {
  virtual ~ISessionRecorder() = default;
  virtual void begin(uint32_t session, const Settings &s, Millis now) = 0;
  virtual void sample(const Snapshot &snapshot) = 0;
  virtual void event(Event event, const Snapshot &snapshot) = 0;
  virtual void end(EndReason reason, const Snapshot &snapshot) = 0;
};
struct ITelemetry {
  virtual ~ITelemetry() = default;
  virtual void publish(const Snapshot &snapshot) = 0;
};
}  // namespace vega
