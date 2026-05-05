#pragma once

#include "LvglDisplay.h"
#include "TouchPanel.h"
#include "esp_timer.h"
#include <cstdint>
#include <string>
#include <functional>

// LED modes
enum LedMode {
    LED_MODE_STATIC,
    LED_MODE_GLOW,
    LED_MODE_RAINBOW
};

// Callback types for MQTT communication
using LightMQTTPublishCallback = std::function<void(const std::string& topic, const std::string& message)>;
using LightMQTTConnectedCallback = std::function<bool()>;
using LightMQTTConfiguredCallback = std::function<bool()>;

class LightController {
private:
    TouchPanel* touchPanel;

    // Setpoint and state
    int setpoint;
    int lastSetpoint;
    int lastSentSetpoint;
    uint16_t lastSentColor;
    int64_t lastSetpointChangeTime;
    int64_t lastColorChangeTime;
    int64_t lastWaterLevelSentTime;
    int64_t lastSetPointDrawnTime;
    int64_t lastScreenUpdateTime;
    int64_t lastSetpointQueryTime;

    // Water level (brightness)
    int waterLevel;
    int lastWaterLevel;

    // Color
    float colorHue;
    float lastColorHue;
    bool colorChange;

    // UI state
    bool initialized;
    bool pageBackRequested;
    bool waterLevelChange;

    // Touch tracking
    int firstTouchX;
    int firstTouchY;

    // LED mode
    LedMode currentMode;
    bool modeChangeRequested;

    // MQTT callbacks
    LightMQTTPublishCallback mqttPublishCallback;
    LightMQTTConnectedCallback mqttConnectedCallback;
    LightMQTTConfiguredCallback mqttConfiguredCallback;

    // Private methods
    void drawStaticElements();
    void drawSetPoint();
    void drawWaterLevel(int level);
    void drawModeText();
    void sendSetpointToServer();
    void requestCurrentBrightnessFromServer();
    uint16_t hsvToRgb565(float hue, float saturation, float value);
    void checkTouchInput();
    void updateColorSpectrum();
    void handleModeChange();

    // Helper
    int64_t millis() const { return esp_timer_get_time() / 1000; }

    template<typename T>
    static T constrain(T value, T minVal, T maxVal) {
        return value < minVal ? minVal : (value > maxVal ? maxVal : value);
    }

public:
    explicit LightController(TouchPanel* touch);

    // Main update method
    void updateScreen();

    // Setpoint control
    void setSetpoint(int value);
    void incrementSetpoint();
    void decrementSetpoint();

    // Navigation
    bool isPageBackRequested() const { return pageBackRequested; }
    void resetPageBackRequest();

    // MQTT callbacks
    void setMQTTPublishCallback(LightMQTTPublishCallback callback) { mqttPublishCallback = callback; }
    void setMQTTConnectedCallback(LightMQTTConnectedCallback callback) { mqttConnectedCallback = callback; }
    void setMQTTConfiguredCallback(LightMQTTConfiguredCallback callback) { mqttConfiguredCallback = callback; }

    // Process incoming brightness value
    void onBrightnessReceived(int brightness);
};
