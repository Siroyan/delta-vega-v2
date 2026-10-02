#pragma once
#include "domain/types.h"
#include "ports/ports.h"
#include "presentation/presenter.h"
#include "course_catalog.h"

namespace tab5 {
using vega::Command;
using vega::CommandKind;
constexpr size_t kMaxPlanChoices = 12;
struct PlanChoice {
  char filename[48]{};
  char label[48]{};
  bool valid = false;
  char error[48]{};
};
struct PlanChoices {
  uint8_t course_index = 0;
  uint8_t count = 0;
  uint8_t selected = 0;
  bool sd_available = false;
  PlanChoice items[kMaxPlanChoices]{};
  char message[80]{};
};
// UI sends value commands; the application task exclusively owns domain state.
bool submit(const Command &command);
bool prepareOutputs();
void outputsOff();
bool begin();
bool snapshot(vega::Snapshot &out);
bool strategy(vega::Strategy &out);
bool planChoices(PlanChoices &out);
vega::UiStatus status();
void serialPoll();
void requestLogReadback(uint32_t session = 0);
}  // namespace tab5
