#pragma once
#include <cstddef>

#include "domain/types.h"
namespace vega {
// Base metadata/number fields plus 100 full-width uint64 durations and separators.
constexpr size_t kTelemetryJsonCapacity = 1024 + 21 * kMaxRaceLaps;
constexpr size_t kSessionSampleJsonCapacity = kTelemetryJsonCapacity + 512;
// Returns bytes excluding the terminator, or zero on invalid metadata / insufficient capacity.
size_t telemetryJson(const Snapshot &s, char *out, size_t capacity, const char *machine_id = "pi",
                     const char *memo = "Delta Vega v2");
// SD-only sample: the MQTT fields plus GPS receiver diagnostics.
size_t sessionSampleJson(const Snapshot &s, char *out, size_t capacity,
                         const char *machine_id = "pi", const char *memo = "Delta Vega v2");
}  // namespace vega
