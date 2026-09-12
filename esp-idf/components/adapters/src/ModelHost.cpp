#include "adapters/ModelHost.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_QNOB_MODEL_TRACE
#include "model/TraceHooks.h"
#endif

namespace adapters {
namespace model_host {

namespace {

constexpr const char* TAG = "MODEL";

portMUX_TYPE s_queueLock = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t s_consumer = nullptr;

void lockQueue(void*) { portENTER_CRITICAL(&s_queueLock); }
void unlockQueue(void*) { portEXIT_CRITICAL(&s_queueLock); }

void sleepMs(uint32_t ms, void*) {
    vTaskDelay(pdMS_TO_TICKS(ms == 0 ? 1 : ms));
}

void logWarning(const char* line, void*) {
    ESP_LOGW(TAG, "%s", line);
}

bool guard(void*) {
    return onConsumerTask();
}

model::Millis nowMs(void*) {
    return static_cast<model::Millis>(esp_timer_get_time() / 1000);
}

#if CONFIG_QNOB_MODEL_TRACE
void traceLine(const char* line, void*) {
    ESP_LOGI(TAG, "%s", line);
}

const char* taskName(void*) {
    return pcTaskGetName(nullptr);
}
#endif

}  // namespace

void install(model::AppModel& app) {
    model::MutationQueue& queue = app.mutations();
    queue.setLockHooks(&lockQueue, &unlockQueue, nullptr);
    queue.setSleepHook(&sleepMs, nullptr);
    queue.setLogHook(&logWarning, nullptr);
    queue.setDrainGuard(&guard, nullptr);
    app.setClock(model::Clock{&nowMs, nullptr});

#if CONFIG_QNOB_MODEL_TRACE
    model::trace::Hooks hooks;
    hooks.log = &traceLine;
    hooks.onConsumerTask = &guard;
    hooks.taskName = &taskName;
    model::trace::install(hooks);
    ESP_LOGI(TAG, "model trace on: one line per drain plus one per committed model");
#endif
}

void adoptCurrentTaskAsConsumer() {
    s_consumer = xTaskGetCurrentTaskHandle();
    ESP_LOGI(TAG, "consumer task: %s", pcTaskGetName(nullptr));
}

bool onConsumerTask() {
    return s_consumer != nullptr && xTaskGetCurrentTaskHandle() == s_consumer;
}

}  // namespace model_host
}  // namespace adapters
