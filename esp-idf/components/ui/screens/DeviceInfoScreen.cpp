#include "DeviceInfoScreen.h"
#include "LvglDisplay.h"
#include "esp_log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr const char* TAG = "DeviceInfoScreen";

}  // namespace

DeviceInfoScreen::DeviceInfoScreen(LGFX* graphics, TouchPanel* touch)
    : gfx(graphics),
      touchPanel(touch),
      screenInitialized(false),
      pageBackRequested(false),
      lvglReady(false),
      wifiConnected(false),
      internetConnected(false),
      mqttConnected(false),
      wifiStrength(0),
      networkStatusChanged(true),
      batteryPresenceKnown(false),
      batteryConnected(false),
      batteryPercentageAvailable(false),
      batteryPercentage(-1.0f),
      batteryVoltage(-1.0f),
      batteryChanged(true),
      lastActivityTime(0),
      lastUpdateTime(0),
      root(nullptr),
      titleLabel(nullptr),
      wifiIconLabel(nullptr),
      wifiStatusLabel(nullptr),
      internetIconLabel(nullptr),
      internetStatusLabel(nullptr),
      mqttIconLabel(nullptr),
      mqttStatusLabel(nullptr),
      batteryIconLabel(nullptr),
      batteryStatusLabel(nullptr),
      batteryPercentIconLabel(nullptr),
      batteryPercentLabel(nullptr),
      swipeHintLabel(nullptr) {
}

void DeviceInfoScreen::update() {
    const int64_t currentMillis = millis();

    // Poll network and battery status periodically
    if (currentMillis - lastUpdateTime >= UPDATE_INTERVAL) {
        updateNetworkStatus();

        if (batteryCallback) {
            const DeviceBatteryStatus newBattery = batteryCallback();
            const float nextPercentage = (newBattery.percentage < 0.0f) ? 0.0f :
                                         (newBattery.percentage > 100.0f) ? 100.0f : newBattery.percentage;
            const bool presenceChanged = (newBattery.presenceKnown != batteryPresenceKnown);
            const bool connectionChanged = (newBattery.connected != batteryConnected);
            const bool availabilityChanged = (newBattery.percentageAvailable != batteryPercentageAvailable);
            const bool percentageChanged = newBattery.percentageAvailable &&
                                           std::abs(nextPercentage - batteryPercentage) > 0.5f;
            const bool voltageChanged = std::abs(newBattery.voltage - batteryVoltage) > 0.02f;

            if (presenceChanged || connectionChanged || availabilityChanged || percentageChanged || voltageChanged) {
                batteryPresenceKnown = newBattery.presenceKnown;
                batteryConnected = newBattery.connected;
                batteryPercentageAvailable = newBattery.percentageAvailable;
                batteryPercentage = batteryPercentageAvailable ? nextPercentage : 0.0f;
                batteryVoltage = newBattery.voltage;
                batteryChanged = true;
            }
        }

        lastUpdateTime = currentMillis;
    }

    ensureUi();
    if (lvglReady) {
        const bool forceFullRefresh = !screenInitialized;
        updateUi(forceFullRefresh);
        LvglDisplay::taskHandler();
    }

    // Handle touch gestures
    if (touchPanel && touchPanel->getHasNewGesture()) {
        resetLastActivityTime();
        touch_gesture_t touchGesture = touchPanel->getLastGesture();

        if (touchGesture == GESTURE_SWIPE_DOWN) {
            pageBackRequested = true;
            screenInitialized = false;
            if (root) {
                lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
            }
            ESP_LOGI(TAG, "Swipe down detected, navigating back to environment info");
        }
    }

    // Also navigate back on tap
    if (touchPanel && touchPanel->getHasNewRelease()) {
        resetLastActivityTime();
        pageBackRequested = true;
        screenInitialized = false;
        if (root) {
            lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
        }
        ESP_LOGI(TAG, "Tap release detected, navigating back");
    }
}

void DeviceInfoScreen::ensureUi() {
    if (lvglReady) {
        return;
    }

    if (!LvglDisplay::isInitialized() && !LvglDisplay::init(gfx)) {
        ESP_LOGE(TAG, "LVGL display init failed");
        return;
    }

    lvglReady = true;
    buildUi();
}

void DeviceInfoScreen::buildUi() {
    if (root != nullptr) {
        return;
    }

    const int displayW = gfx->width();
    const int displayH = gfx->height();
    root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, displayW, displayH);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);

    // Title
    titleLabel = lv_label_create(root);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(titleLabel, "DEVICE INFO");
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, std::max(scalePx(20), displayH / 12));

    // Keep text rows responsive for both 240x240 and 466x466 displays.
    const int rowCount = 5;
    const int topInset = std::max(scalePx(56), displayH / 7);
    const int bottomInset = std::max(scalePx(45), displayH / 8);
    const int availableHeight = std::max(1, displayH - topInset - bottomInset);
    const int itemSpacing = std::max(scalePx(24), availableHeight / rowCount);
    const int startY = topInset;
    const int labelX = displayW / 5;
    const int valueX = (displayW * 3) / 5;

    // WiFi Status
    wifiIconLabel = lv_label_create(root);
    lv_obj_set_style_text_color(wifiIconLabel, lv_color_hex(0x8A97A8), 0);
    lv_label_set_text(wifiIconLabel, "WiFi");
    lv_obj_set_pos(wifiIconLabel, labelX, startY);

    wifiStatusLabel = lv_label_create(root);
    lv_obj_set_style_text_color(wifiStatusLabel, lv_color_hex(0xB15F5F), 0);
    lv_label_set_text(wifiStatusLabel, "OFF");
    lv_obj_set_pos(wifiStatusLabel, valueX, startY);

    // Internet Status
    internetIconLabel = lv_label_create(root);
    lv_obj_set_style_text_color(internetIconLabel, lv_color_hex(0x8A97A8), 0);
    lv_label_set_text(internetIconLabel, "Internet");
    lv_obj_set_pos(internetIconLabel, labelX, startY + itemSpacing);

    internetStatusLabel = lv_label_create(root);
    lv_obj_set_style_text_color(internetStatusLabel, lv_color_hex(0xB15F5F), 0);
    lv_label_set_text(internetStatusLabel, "OFF");
    lv_obj_set_pos(internetStatusLabel, valueX, startY + itemSpacing);

    // MQTT Status
    mqttIconLabel = lv_label_create(root);
    lv_obj_set_style_text_color(mqttIconLabel, lv_color_hex(0x8A97A8), 0);
    lv_label_set_text(mqttIconLabel, "MQTT");
    lv_obj_set_pos(mqttIconLabel, labelX, startY + itemSpacing * 2);

    mqttStatusLabel = lv_label_create(root);
    lv_obj_set_style_text_color(mqttStatusLabel, lv_color_hex(0xB15F5F), 0);
    lv_label_set_text(mqttStatusLabel, "OFF");
    lv_obj_set_pos(mqttStatusLabel, valueX, startY + itemSpacing * 2);

    // Battery section
    batteryIconLabel = lv_label_create(root);
    lv_obj_set_style_text_color(batteryIconLabel, lv_color_hex(0x8A97A8), 0);
    lv_label_set_text(batteryIconLabel, "Battery");
    lv_obj_set_pos(batteryIconLabel, labelX, startY + itemSpacing * 3);

    batteryStatusLabel = lv_label_create(root);
    lv_obj_set_style_text_color(batteryStatusLabel, lv_color_hex(0xD7C06E), 0);
    lv_label_set_text(batteryStatusLabel, "Telemetry unavailable");
    lv_obj_set_pos(batteryStatusLabel, valueX, startY + itemSpacing * 3);

    batteryPercentIconLabel = lv_label_create(root);
    lv_obj_set_style_text_color(batteryPercentIconLabel, lv_color_hex(0x8A97A8), 0);
    lv_label_set_text(batteryPercentIconLabel, "Battery %");
    lv_obj_set_pos(batteryPercentIconLabel, labelX, startY + itemSpacing * 4);

    batteryPercentLabel = lv_label_create(root);
    lv_obj_set_style_text_color(batteryPercentLabel, lv_color_hex(0x9BA7B6), 0);
    lv_label_set_text(batteryPercentLabel, "N/A");
    lv_obj_set_pos(batteryPercentLabel, valueX, startY + itemSpacing * 4);

    // Swipe hint
    swipeHintLabel = lv_label_create(root);
    lv_obj_set_style_text_color(swipeHintLabel, lv_color_hex(0x4A5668), 0);
    lv_label_set_text(swipeHintLabel, "Swipe down to go back");
    lv_obj_align(swipeHintLabel, LV_ALIGN_BOTTOM_MID, 0, -std::max(scalePx(18), displayH / 16));

    LvglDisplay::invalidateScreen();
}

void DeviceInfoScreen::updateUi(bool forceFullRefresh) {
    if (!lvglReady || root == nullptr) {
        return;
    }

    if (forceFullRefresh) {
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        screenInitialized = true;
        networkStatusChanged = true;
        batteryChanged = true;
        LvglDisplay::invalidateScreen();
    }

    if (networkStatusChanged || forceFullRefresh) {
        // WiFi status
        if (wifiConnected) {
            std::string bars = getWifiStrengthBars(wifiStrength);
            lv_label_set_text(wifiStatusLabel, bars.c_str());
            lv_obj_set_style_text_color(wifiStatusLabel, lv_color_hex(0x7CD97A), 0);
        } else {
            lv_label_set_text(wifiStatusLabel, "OFF");
            lv_obj_set_style_text_color(wifiStatusLabel, lv_color_hex(0xB15F5F), 0);
        }

        // Internet status
        if (internetConnected) {
            lv_label_set_text(internetStatusLabel, "ON");
            lv_obj_set_style_text_color(internetStatusLabel, lv_color_hex(0x7CD97A), 0);
        } else {
            lv_label_set_text(internetStatusLabel, "OFF");
            lv_obj_set_style_text_color(internetStatusLabel,
                                        wifiConnected ? lv_color_hex(0xD7C06E) : lv_color_hex(0xB15F5F), 0);
        }

        // MQTT status
        if (mqttConnected) {
            lv_label_set_text(mqttStatusLabel, "ON");
            lv_obj_set_style_text_color(mqttStatusLabel, lv_color_hex(0x7CD97A), 0);
        } else {
            lv_label_set_text(mqttStatusLabel, "OFF");
            lv_obj_set_style_text_color(mqttStatusLabel, lv_color_hex(0xB15F5F), 0);
        }

        networkStatusChanged = false;
    }

    if (batteryChanged || forceFullRefresh) {
        const bool telemetryAvailable = batteryPercentageAvailable || batteryVoltage >= 0.0f;

        if (!batteryPresenceKnown) {
            if (telemetryAvailable) {
                lv_label_set_text(batteryStatusLabel, "Presence unknown");
                lv_obj_set_style_text_color(batteryStatusLabel, lv_color_hex(0xD7C06E), 0);
            } else {
                lv_label_set_text(batteryStatusLabel, "Telemetry unavailable");
                lv_obj_set_style_text_color(batteryStatusLabel, lv_color_hex(0x9BA7B6), 0);
            }
        } else if (batteryConnected) {
            lv_label_set_text(batteryStatusLabel, "Connected");
            lv_obj_set_style_text_color(batteryStatusLabel, lv_color_hex(0x7CD97A), 0);
        } else {
            lv_label_set_text(batteryStatusLabel, "Not detected");
            lv_obj_set_style_text_color(batteryStatusLabel, lv_color_hex(0xB15F5F), 0);
        }

        if (batteryPercentageAvailable) {
            char percentText[24];
            if (!batteryPresenceKnown) {
                std::snprintf(percentText, sizeof(percentText), "%.0f%% est", batteryPercentage);
            } else {
                std::snprintf(percentText, sizeof(percentText), "%.0f%%", batteryPercentage);
            }
            lv_label_set_text(batteryPercentLabel, percentText);
            lv_obj_set_style_text_color(batteryPercentLabel, getBatteryColor(batteryPercentage), 0);
        } else {
            lv_label_set_text(batteryPercentLabel, "N/A");
            lv_obj_set_style_text_color(batteryPercentLabel, lv_color_hex(0x9BA7B6), 0);
        }

        batteryChanged = false;
    }
}

void DeviceInfoScreen::updateNetworkStatus() {
    if (!networkStatusCallback) {
        return;
    }

    bool newWifi = false;
    bool newInternet = false;
    bool newMqtt = false;
    int newStrength = 0;
    networkStatusCallback(newWifi, newInternet, newMqtt, newStrength);

    if (newWifi != wifiConnected || newInternet != internetConnected ||
        newMqtt != mqttConnected || newStrength != wifiStrength) {
        wifiConnected = newWifi;
        internetConnected = newInternet;
        mqttConnected = newMqtt;
        wifiStrength = newStrength;
        networkStatusChanged = true;
    }
}

int DeviceInfoScreen::scalePx(int referencePx) const {
    return std::max(1, (referencePx * gfx->width()) / 240);
}

lv_color_t DeviceInfoScreen::getBatteryColor(float percentage) const {
    if (percentage <= 20.0f) {
        return lv_color_hex(0xFF4444);  // Red for low battery
    } else if (percentage <= 40.0f) {
        return lv_color_hex(0xFFAA00);  // Orange for medium-low
    } else if (percentage <= 60.0f) {
        return lv_color_hex(0xFFDD44);  // Yellow for medium
    } else {
        return lv_color_hex(0x7BE85A);  // Green for good
    }
}

std::string DeviceInfoScreen::getWifiStrengthBars(int strength) const {
    // strength is 0-3
    if (strength <= 0) return "[ ]";
    if (strength == 1) return "[|]";
    if (strength == 2) return "[||]";
    return "[|||]";
}

void DeviceInfoScreen::resetLastActivityTime() {
    lastActivityTime = millis();
}

bool DeviceInfoScreen::isPageBackRequested() {
    return pageBackRequested;
}

void DeviceInfoScreen::resetPageBackRequest() {
    pageBackRequested = false;
}

void DeviceInfoScreen::resetScreen() {
    ESP_LOGI(TAG, "resetScreen called - forcing redraw");
    screenInitialized = false;
    networkStatusChanged = true;
    batteryChanged = true;
    if (root != nullptr) {
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    }
    LvglDisplay::invalidateScreen();
}
