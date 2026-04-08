#pragma once

#include <functional>
#include "esp_err.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal_config.h"

// Timeout values (in milliseconds)
#define CPU_FREQ_REDUCE_TIMEOUT_MS  20000   // 20 seconds - reduce CPU to 50%
#define POWER_SAVE_TIMEOUT_MS       30000   // 30 seconds - screen off
#define LIGHT_SLEEP_TIMEOUT_MS      120000  // 2 minutes - light sleep
#define DEEP_SLEEP_TIMEOUT_MS       180000  // 3 minutes - deep sleep

// Wakeup pins - target specific
#if CONFIG_IDF_TARGET_ESP32S3
#define WAKEUP_PIN_TOUCH        GPIO_NUM_5   // Touch interrupt (CST816S INT direct to GPIO)
#define WAKEUP_PIN_KNOB         GPIO_NUM_18  // UART RX from knob
#elif CONFIG_IDF_TARGET_ESP32C6
// C6 board: FT6146 touch INT goes through TCA9554 I2C expander (EXIO0).
// TCA9554 INT# output is wired to GPIO 1 - use that as touch wake source.
#define WAKEUP_PIN_TOUCH        GPIO_NUM_1   // TCA9554 INT# (touch INT via expander)
#define WAKEUP_PIN_PWR_KEY      GPIO_NUM_2   // Power button (PWR_KEY)
#define WAKEUP_PIN_KNOB         GPIO_NUM_16  // UART RX from knob
#endif

// Callbacks
using PowerSaveModeCallback = std::function<void(bool enabled)>;
using PreSleepCallback = std::function<void()>;

#ifdef __cplusplus

class SleepHandler {
public:
    // Singleton access
    static SleepHandler* getInstance(SemaphoreHandle_t* mutex = nullptr);

    // Initialization
    esp_err_t initialize();

    // Activity tracking
    void resetActivityTime();
    void registerActivity();

    // Enable/Disable
    void enable();
    void disable();
    bool isEnabled() const { return enabled; }

    // Power save mode
    bool isPowerSaveMode() const { return power_save_enabled; }
    bool isCpuFreqReduced() const { return cpu_freq_reduced; }

    // Timeout configuration
    void setInactivityTimeout(uint32_t timeout_ms);
    void setCpuFreqReduceTimeout(uint32_t timeout_ms);
    void setPowerSaveTimeout(uint32_t timeout_ms);
    void setLightSleepTimeout(uint32_t timeout_ms);
    void setDeepSleepTimeout(uint32_t timeout_ms);

    // Callbacks
    void setPowerSaveModeCallback(PowerSaveModeCallback callback) { on_power_save_mode = callback; }
    void setPreSleepCallback(PreSleepCallback callback) { on_pre_sleep = callback; }

    // Main check - call regularly
    void checkActivity();

    // Manual sleep control
    void enterDeepSleep();
    void enterLightSleep(uint32_t duration_ms = 0);

    // Wakeup information
    esp_sleep_wakeup_cause_t getWakeupCause() const;
    const char* getWakeupCauseString() const;

private:
    // Private constructor for singleton
    SleepHandler(SemaphoreHandle_t* mutex);
    ~SleepHandler();

    // Prevent copying
    SleepHandler(const SleepHandler&) = delete;
    SleepHandler& operator=(const SleepHandler&) = delete;

    SemaphoreHandle_t* mutex_ptr;
    bool initialized;
    bool enabled;
    bool cpu_freq_reduced;
    bool power_save_enabled;
    bool light_sleep_enabled;

    int64_t last_activity_time;
    uint32_t cpu_freq_reduce_timeout_ms;
    uint32_t power_save_timeout_ms;
    uint32_t light_sleep_timeout_ms;
    uint32_t deep_sleep_timeout_ms;
    uint32_t original_cpu_freq_mhz;

    PowerSaveModeCallback on_power_save_mode;
    PreSleepCallback on_pre_sleep;

    // Helper methods
    void reduceCpuFrequency();
    void restoreCpuFrequency();
    void configureWakeupSources();
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

// C-compatible interface
typedef void* sleep_handler_handle_t;

sleep_handler_handle_t sleep_handler_get_instance(void* mutex);
esp_err_t sleep_handler_init(sleep_handler_handle_t handle);
void sleep_handler_enable(sleep_handler_handle_t handle);
void sleep_handler_disable(sleep_handler_handle_t handle);
void sleep_handler_reset_activity(sleep_handler_handle_t handle);
void sleep_handler_check_activity(sleep_handler_handle_t handle);
void sleep_handler_enter_deep_sleep(sleep_handler_handle_t handle);
bool sleep_handler_is_power_save_mode(sleep_handler_handle_t handle);

#ifdef __cplusplus
}
#endif
