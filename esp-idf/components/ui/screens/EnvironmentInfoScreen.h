#pragma once

#include "LvglDisplay.h"
#include "TouchPanel.h"
#include "esp_timer.h"
#include <cstdint>
#include <functional>
#include <string>
#include <lvgl.h>

// Callback types for external data sources
using DateTimeCallback = std::function<void(std::string& date, std::string& time, std::string& dayOfWeek)>;
using TemperatureCallback = std::function<float()>;

class EnvironmentInfoScreen {
private:
    TouchPanel* touchPanel;

    bool screenInitialized;
    bool pageBackRequested;
    bool deviceInfoRequested;
    bool lvglReady;
    bool ignoreNextRelease;
    int64_t activatedAtMs;

    // Date and time
    std::string currentDate;
    std::string currentTime;
    std::string formattedDate;
    std::string currentDayOfWeek;
    std::string lastFormattedDate;
    std::string lastFormattedTime;
    bool colonVisible;
    int64_t lastColonToggleTime;
    static constexpr int64_t COLON_BLINK_INTERVAL = 1000;

    // Temperature data
    float indoorTemp;
    float outdoorTemp;

    // Flags for redrawing specific elements
    bool indoorTempChanged;
    bool outdoorTempChanged;
    bool dateTimeChanged;

    // Timing for updates
    int64_t lastActivityTime;
    int64_t lastUpdateTime;
    bool inactivityTimeoutReached;

    // Arc configuration
    static constexpr float ARC_START_ANGLE = 120.0f;
    static constexpr float ARC_LENGTH = 300.0f;
    static constexpr int ARC_RANGE_MAX = 1000;
    static constexpr int64_t ANIMATION_DURATION = 665;  // 950ms * 0.7 — 30% faster
    static constexpr int64_t UPDATE_INTERVAL = 5000;
    static constexpr int64_t INACTIVITY_TIMEOUT = 60000;
    static constexpr int64_t STALE_RELEASE_GUARD_MS = 300;


    // LVGL widgets (text + center disc + tracks only during animation)
    lv_obj_t* root;
    lv_obj_t* outdoorTrackArc;
    lv_obj_t* indoorTrackArc;
    lv_obj_t* outdoorArc;
    lv_obj_t* indoorArc;
    lv_obj_t* centerDisc;
    lv_obj_t* timeLabel;
    lv_obj_t* dayLabel;
    lv_obj_t* dateLabel;
    lv_obj_t* indoorTempLabel;
    lv_obj_t* outdoorTempLabel;

    int currentIndoorArcValue;
    int currentOutdoorArcValue;
    int targetIndoorArcValue;
    int targetOutdoorArcValue;
    int currentIndoorTrackValue;
    int currentOutdoorTrackValue;
    bool animationPending;

    // Callbacks for external data
    DateTimeCallback dateTimeCallback;
    TemperatureCallback outdoorTempCallback;

    // LVGL rendering
    void ensureUi();
    void buildUi();
    void updateUi(bool forceFullRefresh);
    void positionTemperatureLabels();
    void applyIndoorArcFromValue(int value);
    void applyOutdoorArcFromValue(int value);
    void applyIndoorTrackFromValue(int value);
    void applyOutdoorTrackFromValue(int value);

    // Arc animations
    void startAnimations();
    void startIndoorArcAnimation(int targetValue, bool immediate = false, uint32_t delayMs = 0);
    void startOutdoorArcAnimation(int targetValue, bool immediate = false, uint32_t delayMs = 0);
    void startIndoorTrackAnimation(int targetValue, bool immediate = false, uint32_t delayMs = 0);
    void startOutdoorTrackAnimation(int targetValue, bool immediate = false, uint32_t delayMs = 0);
    static void indoorArcAnimExec(void* var, int32_t value);
    static void outdoorArcAnimExec(void* var, int32_t value);
    static void indoorTrackAnimExec(void* var, int32_t value);
    static void outdoorTrackAnimExec(void* var, int32_t value);

    // Data helpers
    void formatDate();
    std::string getMonthName(int month);
    lv_color_t getTemperatureColor(float temperature, bool isIndoor);
    std::string getTimeStringForDisplay() const;
    int scalePx(int referencePx) const;

    // Helper functions
    int64_t millis() const { return esp_timer_get_time() / 1000; }

    template<typename T>
    static T constrain(T value, T minVal, T maxVal) {
        return value < minVal ? minVal : (value > maxVal ? maxVal : value);
    }

public:
    explicit EnvironmentInfoScreen(TouchPanel* touch);
    void deactivate();
    void activate();

    // Main update method
    void update();

    // Data update methods
    void updateDateTime(const std::string& date, const std::string& time, const std::string& dayOfWeek = "");
    void updateIndoorTemperature(float temperature);
    void updateOutdoorTemperature(float temperature);

    // Callbacks for external data sources
    void setDateTimeCallback(DateTimeCallback callback) { dateTimeCallback = callback; }
    void setOutdoorTempCallback(TemperatureCallback callback) { outdoorTempCallback = callback; }

    // Activity tracking
    void resetLastActivityTime();
    bool isInactivityTimeoutReached();

    // Page navigation
    bool isPageBackRequested();
    void resetPageBackRequest();
    bool isDeviceInfoRequested();
    void resetDeviceInfoRequest();
    void resetScreen();
};
