#pragma once
#include <cstddef>

#include "domain/types.h"
namespace vega {
// Returns bytes excluding the terminator, or zero on invalid metadata / insufficient capacity.
size_t telemetryJson(const Snapshot &s, char *out, size_t capacity, const char *machine_id = "pi",
                     const char *memo = "Delta Vega v2");
}  // namespace vega
