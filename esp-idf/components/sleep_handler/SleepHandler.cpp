#include "SleepHandler.h"
#include "hal_init.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_clk_tree.h"
#include "soc/rtc.h"

static const char *TAG = "SleepHandler";

// Singleton instance
static SleepHandler* s_instance = nullptr;

SleepHandler* SleepHandler::getInstance(SemaphoreHandle_t* mutex)
{
    if (s_instance == nullptr) {
        s_instance = new SleepHandler(mutex);
    }
    return s_instance;
}

SleepHandler::SleepHandler(SemaphoreHandle_t* mutex)
    : mutex_ptr(mutex),
      initialized(false),
      enabled(false),
      cpu_freq_reduced(false),
      power_save_enabled(false),
      light_sleep_enabled(false),
      last_activity_time(0),
      cpu_freq_reduce_timeout_ms(CPU_FREQ_REDUCE_TIMEOUT_MS),
      power_save_timeout_ms(POWER_SAVE_TIMEOUT_MS),
      light_sleep_timeout_ms(LIGHT_SLEEP_TIMEOUT_MS),
      deep_sleep_timeout_ms(DEEP_SLEEP_TIMEOUT_MS),
      original_cpu_freq_mhz(0),
      on_power_save_mode(nullptr),
      on_pre_sleep(nullptr)
{
    last_activity_time = esp_timer_get_time() / 1000;

    // Get current CPU frequency
    uint32_t freq_hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &freq_hz);
    original_cpu_freq_mhz = freq_hz / 1000000;
    ESP_LOGI(TAG, "Current CPU frequency: %lu MHz", (unsigned long)original_cpu_freq_mhz);
}

SleepHandler::~SleepHandler()
{
    s_instance = nullptr;
}

esp_err_t SleepHandler::initialize()
{
    ESP_LOGI(TAG, "Initializing sleep handler...");

    // Log wakeup cause if waking from sleep
    esp_sleep_wakeup_cause_t wakeup_cause = getWakeupCause();
    if (wakeup_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "Wakeup cause: %s", getWakeupCauseString());
    }

    resetActivityTime();
    initialized = true;

    ESP_LOGI(TAG, "Sleep handler initialized");
    return ESP_OK;
}

void SleepHandler::resetActivityTime()
{
    last_activity_time = esp_timer_get_time() / 1000;
}

void SleepHandler::registerActivity()
{
    resetActivityTime();

    // Exit light sleep mode if active
    if (light_sleep_enabled) {
        light_sleep_enabled = false;
        ESP_LOGI(TAG, "Exiting light sleep mode due to activity");
    }

    // Exit power save mode if active
    if (power_save_enabled) {
        power_save_enabled = false;
        ESP_LOGI(TAG, "Exiting power save mode due to activity");

        if (on_power_save_mode) {
            on_power_save_mode(false);
        }
    }

    // Restore CPU frequency if reduced
    if (cpu_freq_reduced) {
        restoreCpuFrequency();
    }
}

void SleepHandler::enable()
{
    enabled = true;
    resetActivityTime();
    ESP_LOGI(TAG, "Sleep handler enabled");
}

void SleepHandler::disable()
{
    enabled = false;
    ESP_LOGI(TAG, "Sleep handler disabled");
}

void SleepHandler::setInactivityTimeout(uint32_t timeout_ms)
{
    deep_sleep_timeout_ms = timeout_ms;
}

void SleepHandler::setCpuFreqReduceTimeout(uint32_t timeout_ms)
{
    cpu_freq_reduce_timeout_ms = timeout_ms;
}

void SleepHandler::setPowerSaveTimeout(uint32_t timeout_ms)
{
    power_save_timeout_ms = timeout_ms;
}

void SleepHandler::setLightSleepTimeout(uint32_t timeout_ms)
{
    light_sleep_timeout_ms = timeout_ms;
}

void SleepHandler::setDeepSleepTimeout(uint32_t timeout_ms)
{
    deep_sleep_timeout_ms = timeout_ms;
}

void SleepHandler::checkActivity()
{
    if (!enabled || !initialized) {
        return;
    }

    int64_t current_time = esp_timer_get_time() / 1000;
    int64_t inactivity_time = current_time - last_activity_time;

    // Stage 0: Reduce CPU frequency to 50% at 20s
    if (!cpu_freq_reduced && inactivity_time >= cpu_freq_reduce_timeout_ms) {
        ESP_LOGI(TAG, "Stage 0: Reducing CPU frequency to 50%% after %lld ms", inactivity_time);
        reduceCpuFrequency();
    }

    // Stage 1: Power save mode (screen off) at 30s
    if (!power_save_enabled && inactivity_time >= power_save_timeout_ms) {
        ESP_LOGI(TAG, "Stage 1: Entering power save mode (screen off) after %lld ms", inactivity_time);
        power_save_enabled = true;

        if (on_power_save_mode) {
            on_power_save_mode(true);
        }
    }

    // Stage 2: Light sleep at 2m
    if (!light_sleep_enabled && inactivity_time >= light_sleep_timeout_ms &&
        inactivity_time < deep_sleep_timeout_ms) {

        ESP_LOGI(TAG, "Stage 2: Entering light sleep after %lld ms", inactivity_time);
        light_sleep_enabled = true;

        // Enter light sleep with GPIO wakeup
        enterLightSleep(0);  // 0 = indefinite, rely on GPIO wakeup

        // If we wake up here, reset the light sleep flag
        light_sleep_enabled = false;
    }

    // Stage 3: Deep sleep at 3m
    if (inactivity_time >= deep_sleep_timeout_ms) {
        ESP_LOGI(TAG, "Stage 3: Entering deep sleep after %lld ms", inactivity_time);
        enterDeepSleep();
    }
}

void SleepHandler::reduceCpuFrequency()
{
    if (cpu_freq_reduced || original_cpu_freq_mhz == 0) {
        return;
    }

    // Calculate target frequency (50% of original)
    uint32_t target_freq_mhz = original_cpu_freq_mhz / 2;

    // Ensure minimum frequency (typically 40 MHz for ESP32-S3, 80 MHz for ESP32-C6)
#if CONFIG_IDF_TARGET_ESP32S3
    const uint32_t min_freq_mhz = 40;
#elif CONFIG_IDF_TARGET_ESP32C6
    const uint32_t min_freq_mhz = 80;
#else
    const uint32_t min_freq_mhz = 80;
#endif

    if (target_freq_mhz < min_freq_mhz) {
        target_freq_mhz = min_freq_mhz;
    }

    ESP_LOGI(TAG, "Reducing CPU frequency from %lu MHz to %lu MHz",
             (unsigned long)original_cpu_freq_mhz, (unsigned long)target_freq_mhz);

    // Set the CPU frequency using RTC clock control
    rtc_cpu_freq_config_t config;
    bool success = rtc_clk_cpu_freq_mhz_to_config(target_freq_mhz, &config);

    if (success) {
        rtc_clk_cpu_freq_set_config(&config);
        cpu_freq_reduced = true;
        ESP_LOGI(TAG, "CPU frequency reduced successfully");
    } else {
        ESP_LOGW(TAG, "Failed to reduce CPU frequency to %lu MHz", (unsigned long)target_freq_mhz);
    }
}

void SleepHandler::restoreCpuFrequency()
{
    if (!cpu_freq_reduced || original_cpu_freq_mhz == 0) {
        return;
    }

    ESP_LOGI(TAG, "Restoring CPU frequency to %lu MHz", (unsigned long)original_cpu_freq_mhz);

    rtc_cpu_freq_config_t config;
    bool success = rtc_clk_cpu_freq_mhz_to_config(original_cpu_freq_mhz, &config);

    if (success) {
        rtc_clk_cpu_freq_set_config(&config);
        cpu_freq_reduced = false;
        ESP_LOGI(TAG, "CPU frequency restored successfully");
    } else {
        ESP_LOGW(TAG, "Failed to restore CPU frequency to %lu MHz", (unsigned long)original_cpu_freq_mhz);
    }
}

void SleepHandler::configureWakeupSources()
{
    ESP_LOGI(TAG, "configureWakeupSources() called");
    ESP_LOGI(TAG, "Configuring wakeup sources...");

#if CONFIG_IDF_TARGET_ESP32S3
    // ESP32-S3: Use EXT1 wakeup with RTC GPIO
    uint64_t wakeup_pin_mask = 0;

    // Configure touch wakeup pin
    if (rtc_gpio_is_valid_gpio(WAKEUP_PIN_TOUCH)) {
        rtc_gpio_init(WAKEUP_PIN_TOUCH);
        rtc_gpio_set_direction(WAKEUP_PIN_TOUCH, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_en(WAKEUP_PIN_TOUCH);
        rtc_gpio_pulldown_dis(WAKEUP_PIN_TOUCH);
        wakeup_pin_mask |= (1ULL << WAKEUP_PIN_TOUCH);
        ESP_LOGI(TAG, "Touch wakeup pin %d configured", WAKEUP_PIN_TOUCH);
    }

    // Configure knob UART RX wakeup pin
    if (rtc_gpio_is_valid_gpio(WAKEUP_PIN_KNOB)) {
        rtc_gpio_init(WAKEUP_PIN_KNOB);
        rtc_gpio_set_direction(WAKEUP_PIN_KNOB, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_en(WAKEUP_PIN_KNOB);
        rtc_gpio_pulldown_dis(WAKEUP_PIN_KNOB);
        wakeup_pin_mask |= (1ULL << WAKEUP_PIN_KNOB);
        ESP_LOGI(TAG, "Knob wakeup pin %d configured", WAKEUP_PIN_KNOB);
    }

    if (wakeup_pin_mask != 0) {
        esp_sleep_enable_ext1_wakeup(wakeup_pin_mask, ESP_EXT1_WAKEUP_ANY_LOW);
        ESP_LOGI(TAG, "EXT1 wakeup configured with mask 0x%llX", wakeup_pin_mask);
    }

#elif CONFIG_IDF_TARGET_ESP32C6
    // ESP32-C6: FT6146 touch INT is routed through TCA9554 I2C expander.
    // TCA9554 INT# output is wired to GPIO 1 (EXIO_INT_PIN).
    // Wake sources: GPIO 1 (touch via expander) + GPIO 2 (power button).

    uint64_t wakeup_mask = (1ULL << WAKEUP_PIN_TOUCH) | (1ULL << WAKEUP_PIN_PWR_KEY);

    // Configure both wake-up pins as inputs with pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = wakeup_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_LOW_LEVEL,
    };
    gpio_config(&io_conf);

    // Clear any pending TCA9554 interrupt by reading its input register.
    // This ensures INT# starts HIGH so a new touch event pulls it LOW.
    hal_exio_clear_interrupt();

    // Enable GPIO wakeup for light sleep
    gpio_wakeup_enable((gpio_num_t)WAKEUP_PIN_TOUCH, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)WAKEUP_PIN_PWR_KEY, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Also enable for deep sleep
    esp_deep_sleep_enable_gpio_wakeup(wakeup_mask, ESP_GPIO_WAKEUP_GPIO_LOW);

    ESP_LOGI(TAG, "GPIO wakeup configured: touch (pin %d via TCA9554) + PWR_KEY (pin %d)",
             WAKEUP_PIN_TOUCH, WAKEUP_PIN_PWR_KEY);
#endif
}

void SleepHandler::enterDeepSleep()
{
    ESP_LOGI(TAG, "enterDeepSleep() called");
    if (!enabled) {
        ESP_LOGW(TAG, "Sleep handler disabled, not entering deep sleep");
        return;
    }

    ESP_LOGI(TAG, "Preparing for deep sleep...");

    // Call pre-sleep callback (e.g., to turn off display)
    if (on_pre_sleep) {
        on_pre_sleep();
    }

    // Configure wakeup sources
    configureWakeupSources();

    // Flush logs
    ESP_LOGI(TAG, "Entering deep sleep now...");
    vTaskDelay(pdMS_TO_TICKS(100));

    // Enter deep sleep
    esp_deep_sleep_start();

    // This line is never reached
}

void SleepHandler::enterLightSleep(uint32_t duration_ms)
{
    ESP_LOGI(TAG, "Entering light sleep for %lu ms", duration_ms);

    if (duration_ms > 0) {
        esp_sleep_enable_timer_wakeup(duration_ms * 1000ULL);
    }

    configureWakeupSources();

    esp_light_sleep_start();

    // Light sleep returns here after wakeup
    ESP_LOGI(TAG, "Woke from light sleep: %s", getWakeupCauseString());
    resetActivityTime();
}

esp_sleep_wakeup_cause_t SleepHandler::getWakeupCause() const
{
    return esp_sleep_get_wakeup_cause();
}

const char* SleepHandler::getWakeupCauseString() const
{
    switch (getWakeupCause()) {
        case ESP_SLEEP_WAKEUP_UNDEFINED:    return "Undefined (power-on)";
        case ESP_SLEEP_WAKEUP_ALL:          return "All";
        case ESP_SLEEP_WAKEUP_EXT0:         return "EXT0 (single GPIO)";
        case ESP_SLEEP_WAKEUP_EXT1:         return "EXT1 (multiple GPIO)";
        case ESP_SLEEP_WAKEUP_TIMER:        return "Timer";
        case ESP_SLEEP_WAKEUP_TOUCHPAD:     return "Touchpad";
        case ESP_SLEEP_WAKEUP_ULP:          return "ULP";
        case ESP_SLEEP_WAKEUP_GPIO:         return "GPIO";
        case ESP_SLEEP_WAKEUP_UART:         return "UART";
        default:                            return "Unknown";
    }
}

// C-compatible interface
extern "C" {

sleep_handler_handle_t sleep_handler_get_instance(void* mutex)
{
    return SleepHandler::getInstance(static_cast<SemaphoreHandle_t*>(mutex));
}

esp_err_t sleep_handler_init(sleep_handler_handle_t handle)
{
    return static_cast<SleepHandler*>(handle)->initialize();
}

void sleep_handler_enable(sleep_handler_handle_t handle)
{
    static_cast<SleepHandler*>(handle)->enable();
}

void sleep_handler_disable(sleep_handler_handle_t handle)
{
    static_cast<SleepHandler*>(handle)->disable();
}

void sleep_handler_reset_activity(sleep_handler_handle_t handle)
{
    static_cast<SleepHandler*>(handle)->resetActivityTime();
}

void sleep_handler_check_activity(sleep_handler_handle_t handle)
{
    static_cast<SleepHandler*>(handle)->checkActivity();
}

void sleep_handler_enter_deep_sleep(sleep_handler_handle_t handle)
{
    static_cast<SleepHandler*>(handle)->enterDeepSleep();
}

bool sleep_handler_is_power_save_mode(sleep_handler_handle_t handle)
{
    return static_cast<SleepHandler*>(handle)->isPowerSaveMode();
}

}
