#pragma once

#include "LvglDisplay.h"
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
struct DeviceSoftwareUpdateState {
    bool configured = false;
    bool busy = false;
    bool updateAvailable = false;
    std::string currentVersion;
    std::string availableVersion;
    std::string statusText;
};
using SoftwareUpdateStatusCallback = std::function<DeviceSoftwareUpdateState()>;
using SoftwareUpdateActionCallback = std::function<void()>;

class DeviceInfoScreen {
private:
    TouchPanel* touchPanel;

    bool screenInitialized;
    bool pageBackRequested;
    bool lvglReady;
    bool ignoreNextRelease;
    int64_t activatedAtMs;

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

    // Software update state
    bool softwareChanged;
    bool softwareUpdateConfigured;
    bool softwareUpdateBusy;
    bool softwareUpdateAvailable;
    std::string currentSoftwareVersion;
    std::string availableSoftwareVersion;
    std::string softwareStatusText;

    // Timing for updates
    int64_t lastActivityTime;
    int64_t lastUpdateTime;

    static constexpr int64_t UPDATE_INTERVAL = 2000;
    static constexpr int64_t STALE_RELEASE_GUARD_MS = 300;

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
    lv_obj_t* softwareIconLabel;
    lv_obj_t* softwareVersionLabel;
    lv_obj_t* updateButton;
    lv_obj_t* updateButtonLabel;
    lv_obj_t* softwareStatusLabel;
    lv_obj_t* swipeHintLabel;

    // Callbacks
    DeviceNetworkStatusCallback networkStatusCallback;
    BatteryCallback batteryCallback;
    SoftwareUpdateStatusCallback softwareUpdateStatusCallback;
    SoftwareUpdateActionCallback softwareUpdateActionCallback;

    // LVGL rendering
    void ensureUi();
    void buildUi();
    void updateUi(bool forceFullRefresh);
    void updateNetworkStatus();
    void updateSoftwareUpdateState();
    int scalePx(int referencePx) const;
    lv_color_t getBatteryColor(float percentage) const;
    std::string getWifiStrengthBars(int strength) const;
    static void updateButtonEventHandler(lv_event_t* event);

    // Helper functions
    int64_t millis() const { return esp_timer_get_time() / 1000; }

public:
    explicit DeviceInfoScreen(TouchPanel* touch);
    void deactivate();
    void activate();

    // Main update method
    void update();

    // Callbacks for external data sources
    void setNetworkStatusCallback(DeviceNetworkStatusCallback callback) { networkStatusCallback = callback; }
    void setBatteryCallback(BatteryCallback callback) { batteryCallback = callback; }
    void setSoftwareUpdateStatusCallback(SoftwareUpdateStatusCallback callback) { softwareUpdateStatusCallback = callback; }
    void setSoftwareUpdateActionCallback(SoftwareUpdateActionCallback callback) { softwareUpdateActionCallback = callback; }

    // Activity tracking
    void resetLastActivityTime();

    // Page navigation
    bool isPageBackRequested();
    void resetPageBackRequest();
    void resetScreen();
};
