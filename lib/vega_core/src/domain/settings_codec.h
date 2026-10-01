#pragma once

#include <cstddef>

#include "types.h"

namespace vega {
// Reads both the original fixed-size NVS blob and the current Settings blob.
// A failed decode leaves the caller's settings untouched.
bool decodeSettingsBlob(const void *blob, size_t size, Settings &out);
}  // namespace vega
