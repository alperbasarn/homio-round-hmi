#include "BootGuard.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char* TAG = "BootGuard";

// ---------- static member definitions ----------
BootGuard::SafeMode BootGuard::s_mode        = BootGuard::SafeMode::Normal;
bool                BootGuard::s_timerArmed  = false;
TaskHandle_t        BootGuard::s_markHealthyTask = nullptr;

// ---------- public API ----------

esp_err_t BootGuard::begin()
{
    // nvs_flash_init is idempotent; safe to call before NVSManager.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, erasing now");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_handle_t h;
    err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    // Read persisted state (defaults: 0 for each)
    uint8_t in_progress = 0;
    uint8_t crash_count = 0;
    uint8_t safe_level  = 0;

    nvs_get_u8(h, KEY_IN_PROGRESS, &in_progress);  // ignore NOT_FOUND
    nvs_get_u8(h, KEY_CRASH_COUNT, &crash_count);
    nvs_get_u8(h, KEY_SAFE_LEVEL,  &safe_level);

    if (in_progress) {
        // Previous boot never reached the health timer — treat as a crash.
        if (crash_count < 255) {
            crash_count++;
        }

        // Escalate level based on cumulative crash_count.
        if      (crash_count >= THRESHOLD_EMERGENCY && safe_level < 4) { safe_level = 4; }
        else if (crash_count >= THRESHOLD_OFFLINE   && safe_level < 3) { safe_level = 3; }
        else if (crash_count >= THRESHOLD_HEADLESS  && safe_level < 2) { safe_level = 2; }
        else if (crash_count >= THRESHOLD_RESCUE    && safe_level < 1) { safe_level = 1; }

        ESP_LOGW(TAG, "Incomplete prior boot detected — crash_count=%u safe_level=%u",
                 crash_count, safe_level);
    } else {
        // Clean prior boot: reset counters.
        crash_count = 0;
        safe_level  = 0;
    }

    // Persist updated state and set boot_in_progress before anything else runs.
    nvs_set_u8(h, KEY_CRASH_COUNT, crash_count);
    nvs_set_u8(h, KEY_SAFE_LEVEL,  safe_level);
    nvs_set_u8(h, KEY_IN_PROGRESS, 1);
    nvs_commit(h);
    nvs_close(h);

    s_mode = static_cast<SafeMode>(safe_level);

    if (s_mode == SafeMode::Normal) {
        ESP_LOGI(TAG, "Boot guard: Normal mode (crash_count=%u)", crash_count);
    } else {
        ESP_LOGW(TAG, "Boot guard: %s (crash_count=%u)", modeName(), crash_count);
    }

    return ESP_OK;
}

void BootGuard::armHealthTimer()
{
    if (s_timerArmed) {
        return;
    }
    s_timerArmed = true;

    TimerHandle_t t = xTimerCreate(
        "BootHealthTimer",
        pdMS_TO_TICKS(HEALTH_TIMER_MS),
        pdFALSE,                   // one-shot
        nullptr,
        healthTimerCallback
    );
    if (t == nullptr) {
        ESP_LOGE(TAG, "Failed to create health timer — marking healthy immediately");
        markHealthy();
        return;
    }
    if (xTimerStart(t, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start health timer — marking healthy immediately");
        markHealthy();
    } else {
        ESP_LOGI(TAG, "Health timer armed (%u ms)", HEALTH_TIMER_MS);
    }
}

void BootGuard::markHealthy()
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "markHealthy: nvs_open failed: %s", esp_err_to_name(err));
        return;
    }
    nvs_set_u8(h, KEY_IN_PROGRESS, 0);
    nvs_set_u8(h, KEY_CRASH_COUNT, 0);
    nvs_set_u8(h, KEY_SAFE_LEVEL,  0);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "Boot declared healthy — crash counters cleared");
}

BootGuard::SafeMode BootGuard::getMode()
{
    return s_mode;
}

bool BootGuard::isInSafeMode()
{
    return s_mode != SafeMode::Normal;
}

const char* BootGuard::modeName()
{
    switch (s_mode) {
        case SafeMode::Normal:           return "Normal";
        case SafeMode::Rescue:           return "Rescue Mode";
        case SafeMode::HeadlessRecovery: return "Headless Recovery";
        case SafeMode::OfflineRecovery:  return "Offline Recovery";
        case SafeMode::Emergency:        return "Emergency";
        default:                         return "Unknown";
    }
}

// ---------- private ----------

// Task body: waits for a notification from the timer callback, then writes NVS.
void BootGuard::markHealthyTask(void* /*arg*/)
{
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // block until timer fires
    markHealthy();
    vTaskDelete(nullptr);  // self-delete after single use
}

void BootGuard::healthTimerCallback(TimerHandle_t /*xTimer*/)
{
    // Only notify — never touch NVS from the Timer Service task context.
    if (s_markHealthyTask != nullptr) {
        xTaskNotifyGive(s_markHealthyTask);
    } else {
        // Fallback: task wasn't created (shouldn't happen, but be safe).
        markHealthy();
    }
}
