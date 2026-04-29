#include "DeviceInfoScreen.h"
#include "LvglDisplay.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr const char* TAG = "DeviceInfoScreen";
constexpr uint32_t COLOR_BLACK = 0x000000;
constexpr uint32_t COLOR_WHITE = 0xFFFFFF;
constexpr uint32_t COLOR_DARK_GREY = 0x242A31;
constexpr uint32_t COLOR_GREY = 0x7B8794;
constexpr uint32_t COLOR_MUTED = 0x9BA7B6;
constexpr uint32_t COLOR_GREEN = 0x74E365;
constexpr uint32_t COLOR_RED = 0xFF4C4C;
constexpr uint32_t COLOR_BLUETOOTH = 0x2F80FF;

int clampPercent(int value) {
    return std::max(0, std::min(100, value));
}

}  // namespace

DeviceInfoScreen::DeviceInfoScreen(TouchPanel* touch)
    : touchPanel(touch),
      screenInitialized(false),
      pageBackRequested(false),
      pageLeftRequested(false),
      pageRightRequested(false),
      lvglReady(false),
      ignoreNextRelease(false),
      activatedAtMs(0),
      wifiConnected(false),
      internetConnected(false),
      mqttConnected(false),
      wifiStrength(0),
      bluetoothConnected(false),
      connectedSsid(""),
      connectedIp(""),
      networkStatusChanged(true),
      bluetoothChanged(true),
      batteryPresenceKnown(false),
      batteryConnected(false),
      batteryPercentageAvailable(false),
      batteryPercentage(-1.0f),
      batteryVoltage(-1.0f),
      batteryChanged(true),
      softwareChanged(true),
      softwareUpdateConfigured(false),
      softwareUpdateBusy(false),
      softwareUpdateAvailable(false),
      currentSoftwareVersion("unknown"),
      availableSoftwareVersion(""),
      softwareStatusText("Ready to check for updates"),
      brightnessPercent(100),
      soundPercent(50),
      controlsChanged(true),
      lastActivityTime(0),
      lastUpdateTime(0),
      root(nullptr),
      bluetoothContainer(nullptr),
      bluetoothIcon(nullptr),
      wifiContainer(nullptr),
      wifiArcOuter(nullptr),
      wifiArcMiddle(nullptr),
      wifiArcInner(nullptr),
      wifiDot(nullptr),
      wifiCrossA(nullptr),
      wifiCrossB(nullptr),
      batteryContainer(nullptr),
      batteryOutline(nullptr),
      batteryFill(nullptr),
      batteryCap(nullptr),
      leftButtonLabel(nullptr),
      rightButtonLabel(nullptr),
      backChevron(nullptr),
      brightnessBar(nullptr),
      brightnessFill(nullptr),
      soundBar(nullptr),
      soundFill(nullptr),
      brightnessBarX(0),
      brightnessBarY(0),
      brightnessBarW(0),
      brightnessBarH(0),
      soundBarX(0),
      soundBarY(0),
      soundBarW(0),
      soundBarH(0) {
}

void DeviceInfoScreen::activate() {
    pageBackRequested = false;
    pageLeftRequested = false;
    pageRightRequested = false;
    activatedAtMs = millis();
    ignoreNextRelease = (touchPanel != nullptr) &&
                        (touchPanel->isPressed() ||
                         touchPanel->getHasNewRelease() ||
                         touchPanel->getHasNewHoldRelease());
}

void DeviceInfoScreen::update() {
    const int64_t currentMillis = millis();

    if (currentMillis - lastUpdateTime >= UPDATE_INTERVAL) {
        updateNetworkStatus();
        updateBluetoothStatus();
        updateSoftwareUpdateState();

        if (batteryCallback) {
            const DeviceBatteryStatus next = batteryCallback();
            const float nextPercentage = clampPercent(static_cast<int>(std::lround(next.percentage)));
            const bool percentageChanged = next.percentageAvailable &&
                                           std::abs(nextPercentage - batteryPercentage) > 0.5f;
            const bool voltageChanged = std::abs(next.voltage - batteryVoltage) > 0.02f;

            if (next.presenceKnown != batteryPresenceKnown ||
                next.connected != batteryConnected ||
                next.percentageAvailable != batteryPercentageAvailable ||
                percentageChanged ||
                voltageChanged) {
                batteryPresenceKnown = next.presenceKnown;
                batteryConnected = next.connected;
                batteryPercentageAvailable = next.percentageAvailable;
                batteryPercentage = next.percentageAvailable ? nextPercentage : 0.0f;
                batteryVoltage = next.voltage;
                batteryChanged = true;
            }
        }

        updateControlValues();
        lastUpdateTime = currentMillis;
    }

    ensureUi();
    if (lvglReady) {
        const bool forceFullRefresh = !screenInitialized;
        updateUi(forceFullRefresh);
        handleTouch();
        LvglDisplay::taskHandler();
    }
}

void DeviceInfoScreen::ensureUi() {
    if (lvglReady) {
        if (root == nullptr) {
            buildUi();
        }
        return;
    }

    if (!LvglDisplay::isInitialized() && !LvglDisplay::init()) {
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

    const int displayW = LvglDisplay::getWidth();
    const int displayH = LvglDisplay::getHeight();

    root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, displayW, displayH);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(COLOR_BLACK), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);

    const int edgeW = std::max(scalePx(24), displayW / 10);
    const int topH = std::max(scalePx(64), displayH / 5);
    const int bottomH = std::max(scalePx(32), displayH / 10);
    const int barW = std::max(scalePx(55), (displayW * 6) / 50);
    const int barH = std::min(std::max(scalePx(134), displayH / 2),
                              displayH - topH - bottomH - scalePx(22));
    const int barY = (displayH - barH) / 2;
    const int barGap = scalePx(10);
    brightnessBarW = barW;
    brightnessBarH = barH;
    soundBarW = barW;
    soundBarH = barH;
    soundBarX = (displayW / 2) - (barW / 2);
    brightnessBarX = soundBarX - barW - barGap;
    brightnessBarY = barY;
    soundBarY = barY;

    const int iconW = std::max(scalePx(36), displayW / 8);
    const int iconH = std::max(scalePx(29), topH / 2);
    const int iconX = std::min(displayW - edgeW - iconW,
                               soundBarX + barW + scalePx(12));
    const int iconGap = scalePx(8);
    const int batteryY = std::min(displayH - bottomH - iconH,
                                  soundBarY + barH - iconH);
    const int wifiY = std::max(scalePx(10), batteryY - iconH - iconGap);
    const int bluetoothY = std::max(scalePx(4), wifiY - iconH - iconGap);

    bluetoothContainer = lv_obj_create(root);
    lv_obj_remove_style_all(bluetoothContainer);
    lv_obj_set_size(bluetoothContainer, iconW, iconH);
    lv_obj_set_pos(bluetoothContainer, iconX, bluetoothY);
    lv_obj_clear_flag(bluetoothContainer, LV_OBJ_FLAG_SCROLLABLE);

    bluetoothIcon = lv_label_create(bluetoothContainer);
    lv_obj_set_style_text_align(bluetoothIcon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(bluetoothIcon, &lv_font_montserrat_32, 0);
    lv_label_set_text(bluetoothIcon, LV_SYMBOL_BLUETOOTH);
    lv_obj_center(bluetoothIcon);

    wifiContainer = lv_obj_create(root);
    lv_obj_remove_style_all(wifiContainer);
    lv_obj_set_size(wifiContainer, iconW, iconH);
    lv_obj_set_pos(wifiContainer, iconX, wifiY);
    lv_obj_clear_flag(wifiContainer, LV_OBJ_FLAG_SCROLLABLE);

    auto createWifiArc = [&](int size, int yOffset) -> lv_obj_t* {
        lv_obj_t* arc = lv_arc_create(wifiContainer);
        lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
        lv_obj_set_size(arc, size, size);
        lv_obj_align(arc, LV_ALIGN_BOTTOM_MID, 0, yOffset);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, std::max(3, scalePx(4)), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
        lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, 100);
        lv_arc_set_rotation(arc, 0);
        lv_arc_set_angles(arc, 215, 325);
        return arc;
    };

    wifiArcOuter = createWifiArc(std::min(iconW, iconH + scalePx(14)), scalePx(9));
    wifiArcMiddle = createWifiArc(std::min(iconW - scalePx(11), iconH + scalePx(3)), scalePx(6));
    wifiArcInner = createWifiArc(std::min(iconW - scalePx(22), iconH - scalePx(7)), scalePx(3));

    wifiDot = lv_obj_create(wifiContainer);
    lv_obj_remove_style_all(wifiDot);
    lv_obj_set_size(wifiDot, scalePx(5), scalePx(5));
    lv_obj_set_style_radius(wifiDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(wifiDot, LV_ALIGN_BOTTOM_MID, 0, 0);

    wifiCrossA = lv_label_create(wifiContainer);
    lv_obj_set_style_text_color(wifiCrossA, lv_color_hex(COLOR_RED), 0);
    lv_obj_set_style_text_align(wifiCrossA, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(wifiCrossA, "X");
    lv_obj_align(wifiCrossA, LV_ALIGN_CENTER, 0, 0);

    batteryContainer = lv_obj_create(root);
    lv_obj_remove_style_all(batteryContainer);
    lv_obj_set_size(batteryContainer, iconW, iconH);
    lv_obj_set_pos(batteryContainer, iconX, batteryY);
    lv_obj_clear_flag(batteryContainer, LV_OBJ_FLAG_SCROLLABLE);

    const int batteryW = std::max(scalePx(23), iconW - scalePx(10));
    const int batteryH = std::max(scalePx(11), iconH / 2);
    batteryOutline = lv_obj_create(batteryContainer);
    lv_obj_remove_style_all(batteryOutline);
    lv_obj_set_size(batteryOutline, batteryW, batteryH);
    lv_obj_align(batteryOutline, LV_ALIGN_TOP_MID, -scalePx(4), scalePx(7));
    lv_obj_set_style_radius(batteryOutline, scalePx(4), 0);
    lv_obj_set_style_border_width(batteryOutline, std::max(2, scalePx(2)), 0);
    lv_obj_set_style_border_color(batteryOutline, lv_color_hex(COLOR_GREY), 0);
    lv_obj_set_style_bg_opa(batteryOutline, LV_OPA_TRANSP, 0);

    batteryFill = lv_obj_create(batteryContainer);
    lv_obj_remove_style_all(batteryFill);
    lv_obj_set_style_radius(batteryFill, scalePx(3), 0);

    batteryCap = lv_obj_create(batteryContainer);
    lv_obj_remove_style_all(batteryCap);
    lv_obj_set_size(batteryCap, scalePx(3), std::max(scalePx(6), batteryH / 2));
    lv_obj_align_to(batteryCap, batteryOutline, LV_ALIGN_OUT_RIGHT_MID, scalePx(2), 0);
    lv_obj_set_style_radius(batteryCap, scalePx(2), 0);
    lv_obj_set_style_bg_color(batteryCap, lv_color_hex(COLOR_GREY), 0);
    lv_obj_set_style_bg_opa(batteryCap, LV_OPA_COVER, 0);

    leftButtonLabel = lv_label_create(root);
    lv_obj_set_style_text_color(leftButtonLabel, lv_color_hex(COLOR_GREY), 0);
    lv_label_set_text(leftButtonLabel, "");
    lv_obj_align(leftButtonLabel, LV_ALIGN_LEFT_MID, edgeW / 3, 0);

    rightButtonLabel = lv_label_create(root);
    lv_obj_set_style_text_color(rightButtonLabel, lv_color_hex(COLOR_GREY), 0);
    lv_label_set_text(rightButtonLabel, "");
    lv_obj_align(rightButtonLabel, LV_ALIGN_RIGHT_MID, -(edgeW / 3), 0);

    backChevron = lv_label_create(root);
    lv_obj_set_style_text_color(backChevron, lv_color_hex(COLOR_GREY), 0);
    lv_label_set_text(backChevron, "v");
    lv_obj_align(backChevron, LV_ALIGN_BOTTOM_MID, 0, -(bottomH / 3));

    auto createBar = [&](int x, int y) -> lv_obj_t* {
        lv_obj_t* bar = lv_obj_create(root);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, barW, barH);
        lv_obj_set_pos(bar, x, y);
        lv_obj_set_style_radius(bar, scalePx(12), 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_DARK_GREY), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        return bar;
    };

    brightnessBar = createBar(brightnessBarX, brightnessBarY);
    soundBar = createBar(soundBarX, soundBarY);

    brightnessFill = lv_obj_create(root);
    lv_obj_remove_style_all(brightnessFill);
    lv_obj_set_style_radius(brightnessFill, scalePx(12), 0);
    lv_obj_set_style_bg_color(brightnessFill, lv_color_hex(COLOR_WHITE), 0);
    lv_obj_set_style_bg_opa(brightnessFill, LV_OPA_COVER, 0);

    soundFill = lv_obj_create(root);
    lv_obj_remove_style_all(soundFill);
    lv_obj_set_style_radius(soundFill, scalePx(12), 0);
    lv_obj_set_style_bg_color(soundFill, lv_color_hex(COLOR_WHITE), 0);
    lv_obj_set_style_bg_opa(soundFill, LV_OPA_COVER, 0);

    LvglDisplay::invalidateScreen();
}

void DeviceInfoScreen::updateUi(bool forceFullRefresh) {
    if (!lvglReady || root == nullptr) {
        return;
    }

    int fadeTargetBrightness = 0;
    if (forceFullRefresh) {
        fadeTargetBrightness = getTargetDisplayBrightness();
        LvglDisplay::setBrightness(0);
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        screenInitialized = true;
        networkStatusChanged = true;
        bluetoothChanged = true;
        batteryChanged = true;
        softwareChanged = true;
        controlsChanged = true;
        updateNetworkStatus();
        updateBluetoothStatus();
        updateControlValues();
        LvglDisplay::invalidateScreen();
    }

    if (bluetoothChanged || forceFullRefresh) {
        lv_obj_set_style_text_color(bluetoothIcon,
                                    lv_color_hex(bluetoothConnected ? COLOR_BLUETOOTH : COLOR_GREY),
                                    0);
        bluetoothChanged = false;
    }

    if (networkStatusChanged || forceFullRefresh) {
        const int bars = wifiConnected ? getWifiBars(wifiStrength) : 3;
        const lv_color_t wifiColor = lv_color_hex(!wifiConnected ? COLOR_RED :
                                                  (internetConnected ? COLOR_GREEN : COLOR_GREY));
        const lv_color_t hiddenColor = lv_color_hex(COLOR_DARK_GREY);
        setWifiArcVisible(wifiArcInner, bars >= 1 || !wifiConnected, bars >= 1 || !wifiConnected ? wifiColor : hiddenColor);
        setWifiArcVisible(wifiArcMiddle, bars >= 2 || !wifiConnected, bars >= 2 || !wifiConnected ? wifiColor : hiddenColor);
        setWifiArcVisible(wifiArcOuter, bars >= 3 || !wifiConnected, bars >= 3 || !wifiConnected ? wifiColor : hiddenColor);
        lv_obj_set_style_bg_color(wifiDot, wifiColor, 0);
        lv_obj_set_style_bg_opa(wifiDot, LV_OPA_COVER, 0);

        lv_obj_add_flag(wifiCrossA, LV_OBJ_FLAG_HIDDEN);

        networkStatusChanged = false;
    }

    if (batteryChanged || forceFullRefresh) {
        const bool batteryAvailable = batteryConnected && batteryPercentageAvailable;
        const int percent = batteryAvailable ? clampPercent(static_cast<int>(std::lround(batteryPercentage))) : 0;
        const lv_color_t fillColor = batteryAvailable ? getBatteryColor(batteryPercentage) : lv_color_hex(COLOR_GREY);
        const lv_color_t outlineColor = batteryAvailable ? fillColor : lv_color_hex(COLOR_GREY);

        lv_obj_set_style_border_color(batteryOutline, outlineColor, 0);
        lv_obj_set_style_bg_color(batteryCap, outlineColor, 0);

        const int outlineX = lv_obj_get_x(batteryOutline);
        const int outlineY = lv_obj_get_y(batteryOutline);
        const int innerPad = std::max(3, scalePx(3));
        const int maxFillW = std::max(1, lv_obj_get_width(batteryOutline) - (2 * innerPad));
        const int fillW = batteryAvailable ? std::max(1, (maxFillW * percent) / 100) : 1;
        lv_obj_set_pos(batteryFill, outlineX + innerPad, outlineY + innerPad);
        lv_obj_set_size(batteryFill, fillW, std::max(1, lv_obj_get_height(batteryOutline) - (2 * innerPad)));
        lv_obj_set_style_bg_color(batteryFill, fillColor, 0);
        lv_obj_set_style_bg_opa(batteryFill, batteryAvailable ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

        batteryChanged = false;
    }

    if (controlsChanged || forceFullRefresh) {
        setVerticalBar(brightnessFill, brightnessBarY, brightnessBarH, brightnessPercent);
        setVerticalBar(soundFill, soundBarY, soundBarH, soundPercent);
        controlsChanged = false;
    }

    if (forceFullRefresh) {
        LvglDisplay::taskHandler();
        fadeDisplayBrightness(0, fadeTargetBrightness);
    }
}

void DeviceInfoScreen::updateNetworkStatus() {
    bool newWifi = false;
    bool newInternet = false;
    bool newMqtt = false;
    int newStrength = 0;

    if (networkStatusCallback) {
        networkStatusCallback(newWifi, newInternet, newMqtt, newStrength);
    }

    std::string newSsid;
    std::string newIp;
    if (networkDetailsCallback) {
        networkDetailsCallback(newSsid, newIp);
    }

    if (newWifi != wifiConnected ||
        newInternet != internetConnected ||
        newMqtt != mqttConnected ||
        newStrength != wifiStrength ||
        newSsid != connectedSsid ||
        newIp != connectedIp) {
        wifiConnected = newWifi;
        internetConnected = newInternet;
        mqttConnected = newMqtt;
        wifiStrength = newStrength;
        connectedSsid = newSsid;
        connectedIp = newIp;
        networkStatusChanged = true;
    }
}

void DeviceInfoScreen::updateBluetoothStatus() {
    const bool nextConnected = bluetoothStatusCallback ? bluetoothStatusCallback() : false;
    if (nextConnected != bluetoothConnected) {
        bluetoothConnected = nextConnected;
        bluetoothChanged = true;
    }
}

void DeviceInfoScreen::updateSoftwareUpdateState() {
    if (!softwareUpdateStatusCallback) {
        return;
    }

    const DeviceSoftwareUpdateState next = softwareUpdateStatusCallback();
    if (next.configured != softwareUpdateConfigured ||
        next.busy != softwareUpdateBusy ||
        next.updateAvailable != softwareUpdateAvailable ||
        next.currentVersion != currentSoftwareVersion ||
        next.availableVersion != availableSoftwareVersion ||
        next.statusText != softwareStatusText) {
        softwareUpdateConfigured = next.configured;
        softwareUpdateBusy = next.busy;
        softwareUpdateAvailable = next.updateAvailable;
        currentSoftwareVersion = next.currentVersion;
        availableSoftwareVersion = next.availableVersion;
        softwareStatusText = next.statusText;
        softwareChanged = true;
    }
}

void DeviceInfoScreen::updateControlValues() {
    const int nextBrightness = brightnessGetter ? clampPercent(brightnessGetter()) : brightnessPercent;
    const int nextSound = soundGetter ? clampPercent(soundGetter()) : soundPercent;
    if (nextBrightness != brightnessPercent || nextSound != soundPercent) {
        brightnessPercent = nextBrightness;
        soundPercent = nextSound;
        controlsChanged = true;
    }
}

void DeviceInfoScreen::handleTouch() {
    if (touchPanel == nullptr) {
        return;
    }

    const int64_t now = millis();
    if (ignoreNextRelease &&
        !touchPanel->isPressed() &&
        (touchPanel->getHasNewRelease() || touchPanel->getHasNewHoldRelease()) &&
        (now - activatedAtMs) < STALE_RELEASE_GUARD_MS) {
        return;
    }
    if (!touchPanel->isPressed()) {
        ignoreNextRelease = false;
    }

    const int x = touchPanel->getTouchX();
    const int y = touchPanel->getTouchY();
    const int displayW = LvglDisplay::getWidth();
    const int displayH = LvglDisplay::getHeight();
    const int edgeW = std::max(scalePx(24), displayW / 10);
    const int bottomH = std::max(scalePx(32), displayH / 10);

    if (touchPanel->isPressed()) {
        if (pointInRect(x, y, brightnessBarX - scalePx(8), brightnessBarY, brightnessBarW + scalePx(16), brightnessBarH)) {
            brightnessPercent = percentFromBarY(y, brightnessBarY, brightnessBarH);
            controlsChanged = true;
            if (brightnessSetter) {
                brightnessSetter(brightnessPercent);
            }
        } else if (pointInRect(x, y, soundBarX - scalePx(8), soundBarY, soundBarW + scalePx(16), soundBarH)) {
            soundPercent = percentFromBarY(y, soundBarY, soundBarH);
            controlsChanged = true;
            if (soundSetter) {
                soundSetter(soundPercent);
            }
        }
    }

    if (touchPanel->getHasNewRelease() || touchPanel->getHasNewHoldRelease()) {
        if (pointInRect(x, y, 0, displayH - bottomH, displayW, bottomH)) {
            pageBackRequested = true;
        } else if (pointInRect(x, y, 0, 0, edgeW, displayH)) {
            pageLeftRequested = true;
        } else if (pointInRect(x, y, displayW - edgeW, 0, edgeW, displayH)) {
            pageRightRequested = true;
        }
    }
}

void DeviceInfoScreen::setWifiArcVisible(lv_obj_t* arc, bool visible, lv_color_t color) {
    if (arc == nullptr) {
        return;
    }
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, visible ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
}

void DeviceInfoScreen::setVerticalBar(lv_obj_t* fill, int barY, int barH, int percent) {
    if (fill == nullptr) {
        return;
    }
    const int clamped = clampPercent(percent);
    const int fillH = std::max(1, (barH * clamped) / 100);
    const bool isBrightness = fill == brightnessFill;
    const int x = isBrightness ? brightnessBarX : soundBarX;
    const int w = isBrightness ? brightnessBarW : soundBarW;
    lv_obj_set_pos(fill, x, barY + barH - fillH);
    lv_obj_set_size(fill, w, fillH);
}

bool DeviceInfoScreen::pointInRect(int x, int y, int rx, int ry, int rw, int rh) const {
    return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
}

int DeviceInfoScreen::percentFromBarY(int y, int barY, int barH) const {
    const int clampedY = std::max(barY, std::min(barY + barH, y));
    const int value = ((barY + barH - clampedY) * 100) / std::max(1, barH);
    return clampPercent(value);
}

int DeviceInfoScreen::getTargetDisplayBrightness() const {
    const int percent = brightnessGetter ? clampPercent(brightnessGetter()) : clampPercent(brightnessPercent);
    return std::max(0, std::min(255, (percent * 255 + 50) / 100));
}

void DeviceInfoScreen::fadeDisplayBrightness(int from, int to) {
    const int start = std::max(0, std::min(255, from));
    const int end = std::max(0, std::min(255, to));
    const int steps = std::max<int>(1, PAGE_FADE_DURATION_MS / PAGE_FADE_STEP_MS);

    for (int i = 0; i <= steps; ++i) {
        const int value = start + ((end - start) * i) / steps;
        LvglDisplay::setBrightness(static_cast<uint8_t>(value));
        vTaskDelay(pdMS_TO_TICKS(PAGE_FADE_STEP_MS));
    }
    LvglDisplay::setBrightness(static_cast<uint8_t>(end));
}

int DeviceInfoScreen::scalePx(int referencePx) const {
    return std::max(1, (referencePx * LvglDisplay::getWidth()) / 240);
}

lv_color_t DeviceInfoScreen::getBatteryColor(float percentage) const {
    return lv_color_hex(percentage < 20.0f ? COLOR_RED : COLOR_GREEN);
}

int DeviceInfoScreen::getWifiBars(int strength) const {
    if (strength <= 0) {
        return 0;
    }
    if (strength >= 4) {
        return 3;
    }
    return strength;
}

void DeviceInfoScreen::updateButtonEventHandler(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<DeviceInfoScreen*>(lv_event_get_user_data(event));
    if (self == nullptr) {
        return;
    }

    self->resetLastActivityTime();
    if (self->softwareUpdateActionCallback) {
        self->softwareUpdateActionCallback();
    }
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

void DeviceInfoScreen::deactivate() {
    screenInitialized = false;
    if (root != nullptr) {
        const int targetBrightness = getTargetDisplayBrightness();
        fadeDisplayBrightness(targetBrightness, 0);
        LvglDisplay::invalidateScreen();
        LvglDisplay::taskHandler();
        lv_obj_del(root);
        root = nullptr;
        bluetoothContainer = nullptr;
        bluetoothIcon = nullptr;
        wifiContainer = nullptr;
        wifiArcOuter = nullptr;
        wifiArcMiddle = nullptr;
        wifiArcInner = nullptr;
        wifiDot = nullptr;
        wifiCrossA = nullptr;
        wifiCrossB = nullptr;
        batteryContainer = nullptr;
        batteryOutline = nullptr;
        batteryFill = nullptr;
        batteryCap = nullptr;
        leftButtonLabel = nullptr;
        rightButtonLabel = nullptr;
        backChevron = nullptr;
        brightnessBar = nullptr;
        brightnessFill = nullptr;
        soundBar = nullptr;
        soundFill = nullptr;
        LvglDisplay::setBrightness(static_cast<uint8_t>(targetBrightness));
    }
}

void DeviceInfoScreen::resetScreen() {
    ESP_LOGI(TAG, "resetScreen called - forcing redraw");
    screenInitialized = false;
    networkStatusChanged = true;
    bluetoothChanged = true;
    batteryChanged = true;
    softwareChanged = true;
    controlsChanged = true;
    pageBackRequested = false;
    pageLeftRequested = false;
    pageRightRequested = false;

    if (root != nullptr) {
        lv_obj_del(root);
        root = nullptr;
        bluetoothContainer = nullptr;
        bluetoothIcon = nullptr;
        wifiContainer = nullptr;
        wifiArcOuter = nullptr;
        wifiArcMiddle = nullptr;
        wifiArcInner = nullptr;
        wifiDot = nullptr;
        wifiCrossA = nullptr;
        wifiCrossB = nullptr;
        batteryContainer = nullptr;
        batteryOutline = nullptr;
        batteryFill = nullptr;
        batteryCap = nullptr;
        leftButtonLabel = nullptr;
        rightButtonLabel = nullptr;
        backChevron = nullptr;
        brightnessBar = nullptr;
        brightnessFill = nullptr;
        soundBar = nullptr;
        soundFill = nullptr;
    }

    activate();
    LvglDisplay::invalidateScreen();
}
