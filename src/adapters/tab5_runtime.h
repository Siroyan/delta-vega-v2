#pragma once
#include "domain/types.h"
#include "ports/ports.h"
#include "presentation/presenter.h"

namespace tab5 {
using vega::Command;
using vega::CommandKind;
// UI sends value commands; the application task exclusively owns domain state.
bool submit(const Command &command);
bool prepareOutputs();
void outputsOff();
bool begin();
bool snapshot(vega::Snapshot &out);
bool strategy(vega::Strategy &out);
vega::UiStatus status();
void serialPoll();
void requestLogReadback(uint32_t session = 0);
// Non-blocking field-test trace; diagnostic records yield queue space to race samples/events.
void recordUiMarker(bool screen_main, bool model_visible, bool widget_visible, bool gps_fresh,
                    int32_t map_x, int32_t map_y, int32_t backing_x, int32_t backing_y);
}  // namespace tab5
