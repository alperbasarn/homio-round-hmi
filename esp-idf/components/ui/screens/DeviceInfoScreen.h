#pragma once

#include "LGFX_Config.hpp"
#include "TouchPanel.h"
#include "esp_timer.h"
#include <cstdint>
#include <functional>
#include <string>
#include <lvgl.h>

// Callback types for external data sources
using DeviceNetworkStatusCallback = std::function<void(bool& wifi, bool& internet, bool& mqtt, int& strength)>;
struct DeviceBatteryStatus {
    bool presenceKnown = false;
    bool connected = false;
    bool percentageAvailable = false;
    float percentage = -1.0f;
    float voltage = -1.0f;
};
using BatteryCallback = std::function<DeviceBatteryStatus()>;

class DeviceInfoScreen {
private:
    LGFX* gfx;
    TouchPanel* touchPanel;

    bool screenInitialized;
    bool pageBackRequested;
    bool lvglReady;

    // Network status
    bool wifiConnected;
    bool internetConnected;
    bool mqttConnected;
    int wifiStrength;
    bool networkStatusChanged;

    // Battery
    bool batteryPresenceKnown;
    bool batteryConnected;
    bool batteryPercentageAvailable;
    float batteryPercentage;
    float batteryVoltage;
    bool batteryChanged;

    // Timing for updates
    int64_t lastActivityTime;
    int64_t lastUpdateTime;

    static constexpr int64_t UPDATE_INTERVAL = 2000;

    // LVGL widgets
    lv_obj_t* root;
    lv_obj_t* titleLabel;
    lv_obj_t* wifiIconLabel;
    lv_obj_t* wifiStatusLabel;
    lv_obj_t* internetIconLabel;
    lv_obj_t* internetStatusLabel;
    lv_obj_t* mqttIconLabel;
    lv_obj_t* mqttStatusLabel;
    lv_obj_t* batteryIconLabel;
    lv_obj_t* batteryStatusLabel;
    lv_obj_t* batteryPercentIconLabel;
    lv_obj_t* batteryPercentLabel;
    lv_obj_t* swipeHintLabel;

    // Callbacks
    DeviceNetworkStatusCallback networkStatusCallback;
    BatteryCallback batteryCallback;

    // LVGL rendering
    void ensureUi();
    void buildUi();
    void updateUi(bool forceFullRefresh);
    void updateNetworkStatus();
    int scalePx(int referencePx) const;
    lv_color_t getBatteryColor(float percentage) const;
    std::string getWifiStrengthBars(int strength) const;

    // Helper functions
    int64_t millis() const { return esp_timer_get_time() / 1000; }

public:
    DeviceInfoScreen(LGFX* graphics, TouchPanel* touch);

    // Main update method
    void update();

    // Callbacks for external data sources
    void setNetworkStatusCallback(DeviceNetworkStatusCallback callback) { networkStatusCallback = callback; }
    void setBatteryCallback(BatteryCallback callback) { batteryCallback = callback; }

    // Activity tracking
    void resetLastActivityTime();

    // Page navigation
    bool isPageBackRequested();
    void resetPageBackRequest();
    void resetScreen();
};
