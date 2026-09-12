#pragma once
// ModelHost (APP-06, MDL-32): the platform half of the model root. Installs the queue's lock,
// sleep, log and drain-guard hooks, the clock, and, when CONFIG_QNOB_MODEL_TRACE is set, the
// trace sink. The model layer itself stays free of FreeRTOS and ESP headers.
#include "model/AppModel.h"

namespace adapters {
namespace model_host {

// Call once, before any task can post. The consumer task registers itself afterwards.
void install(model::AppModel& app);

// The display task calls this as its first statement so the drain guard and the off-task
// write assertion know which task is the single writer.
void adoptCurrentTaskAsConsumer();

bool onConsumerTask();

}  // namespace model_host
}  // namespace adapters
