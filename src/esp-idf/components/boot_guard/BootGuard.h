#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <cstdint>

/**
 * @brief 4-level escalating safe-mode boot guard.
 *
 * Call BootGuard::begin() as the very first instruction in app_main (before
 * any other hardware or RTOS init).  It uses a dedicated NVS namespace
 * ("boot_guard") so it operates independently of the main app config.
 *
 * Escalation table:
 *   3 consecutive normal-boot crashes → Rescue Mode (level 1)
 *   +2 crashes in Rescue              → Headless Recovery (level 2)
 *   +2 crashes in Headless            → Offline Recovery (level 3)
 *   +2 crashes in Offline             → Emergency (level 4)
 *
 * A boot is declared healthy when armHealthTimer() fires after 15 s,
 * at which point crash_count and safe_mode_level are both reset to 0.
 *
 * Level  | Display      | WiFi     | Portal | BLE | MQTT
 * -------|--------------|----------|--------|-----|-----
 *   0    | Full         | Full     | Full   | Yes | Yes
 *   1    | Sad-face     | AP only  | Yes    | No  | No
 *   2    | Off          | AP only  | Yes    | No  | No
 *   3    | Sad-face     | Off      | Off    | No  | No
 *   4    | Off (serial) | Off      | Off    | No  | No
 */

class BootGuard {
public:
    enum class SafeMode : uint8_t {
        Normal          = 0,
        Rescue          = 1,
        HeadlessRecovery = 2,
        OfflineRecovery  = 3,
        Emergency        = 4,
    };

    /**
     * @brief Must be the first call in app_main.
     *
     * Opens (or creates) the "boot_guard" NVS partition, reads persisted
     * state, increments crash_count when a prior boot was incomplete, updates
     * safe_mode_level, writes boot_in_progress = 1, then saves.
     *
     * nvs_flash_init() is called internally; it is safe to call again from
     * NVSManager — the second call is a no-op.
     */
    static esp_err_t begin();

    /**
     * @brief Arms a one-shot 15-second FreeRTOS timer that calls
     *        markHealthy() automatically.  Invoke after all components are
     *        initialised (at the end of app_main, before tasks start).
     *
     *        Safe to call multiple times; only the first call arms the timer.
     */
    static void armHealthTimer();

    /**
     * @brief Clears boot_in_progress, resets crash_count and safe_mode_level
     *        to 0.  Called automatically by the health timer; may also be
     *        called manually.
     */
    static void markHealthy();

    /** Returns the resolved safe-mode level (valid after begin()). */
    static SafeMode getMode();

    /** Convenience: returns true for any level > Normal. */
    static bool isInSafeMode();

    /** Human-readable label for the current level. */
    static const char* modeName();

private:
    BootGuard() = delete;

    static SafeMode  s_mode;
    static bool      s_timerArmed;

    // NVS key strings (max 15 chars per ESP-IDF requirement)
    static constexpr const char* NVS_NS          = "boot_guard";
    static constexpr const char* KEY_IN_PROGRESS = "boot_ip";
    static constexpr const char* KEY_CRASH_COUNT = "crash_cnt";
    static constexpr const char* KEY_SAFE_LEVEL  = "safe_lvl";

    // Crash thresholds that trigger each escalation
    static constexpr uint8_t THRESHOLD_RESCUE    = 3;  // normal → rescue
    static constexpr uint8_t THRESHOLD_HEADLESS  = 5;  // rescue → headless
    static constexpr uint8_t THRESHOLD_OFFLINE   = 7;  // headless → offline
    static constexpr uint8_t THRESHOLD_EMERGENCY = 9;  // offline → emergency

    static constexpr uint32_t HEALTH_TIMER_MS = 15000;

    static void healthTimerCallback(TimerHandle_t xTimer);
};
