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
void requestLogReadback();
}  // namespace tab5
