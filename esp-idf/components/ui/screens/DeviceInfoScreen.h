#pragma once

#include "LvglDisplay.h"
#include "TouchPanel.h"
#include "esp_timer.h"
#include <cstdint>
#include <functional>
#include <string>
#include <lvgl.h>

using DeviceNetworkStatusCallback = std::function<void(bool& wifi, bool& internet, bool& mqtt, int& strength)>;
using DeviceNetworkDetailsCallback = std::function<void(std::string& ssid, std::string& ip)>;
using DeviceBluetoothStatusCallback = std::function<bool()>;

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
using DevicePercentGetterCallback = std::function<int()>;
using DevicePercentSetterCallback = std::function<void(int)>;

class DeviceInfoScreen {
private:
    TouchPanel* touchPanel;

    bool screenInitialized;
    bool pageBackRequested;
    bool pageLeftRequested;
    bool pageRightRequested;
    bool lvglReady;
    bool ignoreNextRelease;
    int64_t activatedAtMs;

    bool wifiConnected;
    bool internetConnected;
    bool mqttConnected;
    int wifiStrength;
    bool bluetoothConnected;
    std::string connectedSsid;
    std::string connectedIp;
    bool networkStatusChanged;
    bool bluetoothChanged;

    bool batteryPresenceKnown;
    bool batteryConnected;
    bool batteryPercentageAvailable;
    float batteryPercentage;
    float batteryVoltage;
    bool batteryChanged;

    bool softwareChanged;
    bool softwareUpdateConfigured;
    bool softwareUpdateBusy;
    bool softwareUpdateAvailable;
    std::string currentSoftwareVersion;
    std::string availableSoftwareVersion;
    std::string softwareStatusText;

    int brightnessPercent;
    int soundPercent;
    bool controlsChanged;

    int64_t lastActivityTime;
    int64_t lastUpdateTime;

    static constexpr int64_t UPDATE_INTERVAL = 2000;
    static constexpr int64_t STALE_RELEASE_GUARD_MS = 300;
    static constexpr int64_t PAGE_FADE_DURATION_MS = 200;
    static constexpr int64_t PAGE_FADE_STEP_MS = 10;

    lv_obj_t* root;
    lv_obj_t* bluetoothContainer;
    lv_obj_t* bluetoothIcon;
    lv_obj_t* wifiContainer;
    lv_obj_t* wifiArcOuter;
    lv_obj_t* wifiArcMiddle;
    lv_obj_t* wifiArcInner;
    lv_obj_t* wifiDot;
    lv_obj_t* wifiCrossA;
    lv_obj_t* wifiCrossB;
    lv_obj_t* batteryContainer;
    lv_obj_t* batteryOutline;
    lv_obj_t* batteryFill;
    lv_obj_t* batteryCap;
    lv_obj_t* leftButtonLabel;
    lv_obj_t* rightButtonLabel;
    lv_obj_t* backChevron;
    lv_obj_t* brightnessBar;
    lv_obj_t* brightnessFill;
    lv_obj_t* soundBar;
    lv_obj_t* soundFill;

    int brightnessBarX;
    int brightnessBarY;
    int brightnessBarW;
    int brightnessBarH;
    int soundBarX;
    int soundBarY;
    int soundBarW;
    int soundBarH;

    DeviceNetworkStatusCallback networkStatusCallback;
    DeviceNetworkDetailsCallback networkDetailsCallback;
    DeviceBluetoothStatusCallback bluetoothStatusCallback;
    BatteryCallback batteryCallback;
    SoftwareUpdateStatusCallback softwareUpdateStatusCallback;
    SoftwareUpdateActionCallback softwareUpdateActionCallback;
    DevicePercentGetterCallback brightnessGetter;
    DevicePercentSetterCallback brightnessSetter;
    DevicePercentGetterCallback soundGetter;
    DevicePercentSetterCallback soundSetter;

    void ensureUi();
    void buildUi();
    void updateUi(bool forceFullRefresh);
    void updateNetworkStatus();
    void updateBluetoothStatus();
    void updateSoftwareUpdateState();
    void updateControlValues();
    void handleTouch();
    void setWifiArcVisible(lv_obj_t* arc, bool visible, lv_color_t color);
    void setVerticalBar(lv_obj_t* fill, int barY, int barH, int percent);
    bool pointInRect(int x, int y, int rx, int ry, int rw, int rh) const;
    int percentFromBarY(int y, int barY, int barH) const;
    int getTargetDisplayBrightness() const;
    void fadeDisplayBrightness(int from, int to);
    int scalePx(int referencePx) const;
    lv_color_t getBatteryColor(float percentage) const;
    int getWifiBars(int strength) const;
    static void updateButtonEventHandler(lv_event_t* event);

    int64_t millis() const { return esp_timer_get_time() / 1000; }

public:
    explicit DeviceInfoScreen(TouchPanel* touch);
    void deactivate();
    void activate();

    void update();

    void setNetworkStatusCallback(DeviceNetworkStatusCallback callback) { networkStatusCallback = callback; }
    void setNetworkDetailsCallback(DeviceNetworkDetailsCallback callback) { networkDetailsCallback = callback; }
    void setBluetoothStatusCallback(DeviceBluetoothStatusCallback callback) { bluetoothStatusCallback = callback; }
    void setBatteryCallback(BatteryCallback callback) { batteryCallback = callback; }
    void setSoftwareUpdateStatusCallback(SoftwareUpdateStatusCallback callback) { softwareUpdateStatusCallback = callback; }
    void setSoftwareUpdateActionCallback(SoftwareUpdateActionCallback callback) { softwareUpdateActionCallback = callback; }
    void setBrightnessControlCallbacks(DevicePercentGetterCallback getter, DevicePercentSetterCallback setter) {
        brightnessGetter = getter;
        brightnessSetter = setter;
    }
    void setSoundControlCallbacks(DevicePercentGetterCallback getter, DevicePercentSetterCallback setter) {
        soundGetter = getter;
        soundSetter = setter;
    }

    void resetLastActivityTime();

    bool isPageBackRequested();
    void resetPageBackRequest();
    bool isPageLeftRequested() const { return pageLeftRequested; }
    void resetPageLeftRequest() { pageLeftRequested = false; }
    bool isPageRightRequested() const { return pageRightRequested; }
    void resetPageRightRequest() { pageRightRequested = false; }
    void resetScreen();
};
