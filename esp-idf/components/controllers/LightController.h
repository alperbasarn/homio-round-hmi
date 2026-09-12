#pragma once

#include "DisplayPanelBackend.h"
#include "TouchPanel.h"
#include "esp_timer.h"
#include <cstdint>
#include <functional>
#include <lvgl.h>
#include <string>

enum LedMode {
    LED_MODE_STATIC,
    LED_MODE_GLOW,
    LED_MODE_RAINBOW
};

using LightMQTTPublishCallback = std::function<void(const std::string& topic, const std::string& message)>;
using LightMQTTConnectedCallback = std::function<bool()>;
using LightMQTTConfiguredCallback = std::function<bool()>;

class LightController {
private:
    DisplayPanelBackend* display;
    TouchPanel* touchPanel;

    int setpoint;
    int lastSetpoint;
    int lastSentSetpoint;
    uint32_t lastSentColor;
    int64_t lastSetpointChangeTime;
    int64_t lastColorChangeTime;
    int64_t lastWaterLevelSentTime;
    int64_t lastSetPointDrawnTime;
    int64_t lastScreenUpdateTime;
    int64_t lastSetpointQueryTime;

    int waterLevel;
    int lastWaterLevel;
    float colorHue;
    float lastColorHue;
    bool colorChange;
    bool initialized;
    bool pageBackRequested;
    bool waterLevelChange;
    int firstTouchX;
    int firstTouchY;
    LedMode currentMode;
    bool modeChangeRequested;

    lv_obj_t* root;
    lv_obj_t* waterRect;
    lv_obj_t* levelLabel;
    lv_obj_t* modeLabel;
    lv_obj_t* hintLabel;

    LightMQTTPublishCallback mqttPublishCallback;
    LightMQTTConnectedCallback mqttConnectedCallback;
    LightMQTTConfiguredCallback mqttConfiguredCallback;

    void ensureUi();
    void drawStaticElements();
    void drawSetPoint();
    void drawModeText();
    void sendSetpointToServer();
    void requestCurrentBrightnessFromServer();
    uint32_t hsvToRgb(float hue, float saturation, float value) const;
    void checkTouchInput();
    void handleModeChange();
    int scalePx(int referencePx) const;

    int64_t millis() const { return esp_timer_get_time() / 1000; }

    template<typename T>
    static T constrain(T value, T minVal, T maxVal) {
        return value < minVal ? minVal : (value > maxVal ? maxVal : value);
    }

public:
    LightController(DisplayPanelBackend* displayBackend, TouchPanel* touch);

    void updateScreen();
    void setSetpoint(int value);
    void incrementSetpoint();
    void decrementSetpoint();

    bool isPageBackRequested() const { return pageBackRequested; }
    void resetPageBackRequest();

    void setMQTTPublishCallback(LightMQTTPublishCallback callback) { mqttPublishCallback = callback; }
    void setMQTTConnectedCallback(LightMQTTConnectedCallback callback) { mqttConnectedCallback = callback; }
    void setMQTTConfiguredCallback(LightMQTTConfiguredCallback callback) { mqttConfiguredCallback = callback; }

    void onBrightnessReceived(int brightness);
};
