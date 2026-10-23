#pragma once

#include "domain/types.h"

namespace vega {
// Owned by the transport worker. Retain one full state, never a backlog of samples.
// The connection generation makes even a brief disconnect trigger a fresh send.
class LatestTelemetry {
 public:
  void update(const Snapshot &snapshot) {
    latest_ = snapshot;
    has_snapshot_ = dirty_ = true;
  }

  const Snapshot *pending(bool connected, uint32_t connection_generation, Millis now) const {
    if (!connected || !has_snapshot_ ||
        (!dirty_ && sent_ && connection_generation == sent_generation_))
      return nullptr;
    // A moving sample expires. A completed result or a cancellation remains
    // authoritative while offline, even after the normal sample-age limit.
    if (latest_.race.phase == RacePhase::Measuring &&
        (now < latest_.now_ms || now - latest_.now_ms >= 1500))
      return nullptr;
    return &latest_;
  }

  void sent(uint32_t connection_generation) {
    dirty_ = false;
    sent_ = true;
    sent_generation_ = connection_generation;
  }

 private:
  Snapshot latest_{};
  bool has_snapshot_ = false, dirty_ = false, sent_ = false;
  uint32_t sent_generation_ = 0;
};
}  // namespace vega
