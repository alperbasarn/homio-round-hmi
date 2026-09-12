#pragma once

#include "DisplayPanelBackend.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstdint>
#include <lvgl.h>

class InitializationScreen {
private:
    DisplayPanelBackend* display;
    int progressValue;
    int previousProgress;
    bool screenInitialized;
    bool wifiConnected;
    int wifiStrength;
    bool mqttConnected;

    lv_obj_t* root;
    lv_obj_t* progressArc;
    lv_obj_t* logoLabel;
    lv_obj_t* progressLabel;
    lv_obj_t* statusLabel;

    void ensureUi();
    void updateWidgets();
    int scalePx(int referencePx) const;

    int64_t millis() const { return esp_timer_get_time() / 1000; }

    template<typename T>
    static T constrain(T value, T minVal, T maxVal) {
        return std::max(minVal, std::min(value, maxVal));
    }

public:
    explicit InitializationScreen(DisplayPanelBackend* displayBackend);

    void updateScreen();
    void setProgress(int progress);
    void setWiFiStatus(bool connected, int strength);
    void setMQTTStatus(bool isConnected);
    void reset();
};
