#include "LightController.h"
#include "LvglDisplay.h"
#include "esp_log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr const char* TAG = "LightCtrl";

uint8_t channelFromRgb(uint32_t rgb, int shift) {
    return static_cast<uint8_t>((rgb >> shift) & 0xFF);
}

}  // namespace

LightController::LightController(DisplayPanelBackend* displayBackend, TouchPanel* touch)
    : display(displayBackend),
      touchPanel(touch),
      setpoint(50),
      lastSetpoint(-1),
      lastSentSetpoint(-1),
      lastSentColor(0),
      lastSetpointChangeTime(0),
      lastColorChangeTime(0),
      lastWaterLevelSentTime(0),
      lastSetPointDrawnTime(0),
      lastScreenUpdateTime(0),
      lastSetpointQueryTime(0),
      waterLevel(50),
      lastWaterLevel(-1),
      colorHue(0.5f),
      lastColorHue(-1),
      colorChange(false),
      initialized(false),
      pageBackRequested(false),
      waterLevelChange(false),
      firstTouchX(-1),
      firstTouchY(-1),
      currentMode(LED_MODE_STATIC),
      modeChangeRequested(false),
      root(nullptr),
      waterRect(nullptr),
      levelLabel(nullptr),
      modeLabel(nullptr),
      hintLabel(nullptr) {
}

void LightController::ensureUi() {
    if (root != nullptr || display == nullptr || !LvglDisplay::isInitialized()) {
        return;
    }

    if (!LvglDisplay::lock(-1)) {
        ESP_LOGW(TAG, "LVGL lock unavailable while creating light controller UI");
        return;
    }

    root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, display->width(), display->height());
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);

    waterRect = lv_obj_create(root);
    lv_obj_remove_style_all(waterRect);
    lv_obj_set_width(waterRect, display->width());
    lv_obj_set_style_bg_opa(waterRect, LV_OPA_COVER, 0);

    levelLabel = lv_label_create(root);
    lv_obj_set_style_text_color(levelLabel, lv_color_white(), 0);
    lv_obj_align(levelLabel, LV_ALIGN_CENTER, 0, -scalePx(10));

    modeLabel = lv_label_create(root);
    lv_obj_set_style_text_color(modeLabel, lv_color_hex(0xDDE7F5), 0);
    lv_obj_align(modeLabel, LV_ALIGN_BOTTOM_MID, 0, -scalePx(46));

    hintLabel = lv_label_create(root);
    lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x6B778A), 0);
    lv_label_set_text(hintLabel, "Drag up/down brightness, left/right hue");
    lv_obj_align(hintLabel, LV_ALIGN_BOTTOM_MID, 0, -scalePx(18));

    LvglDisplay::unlock();
}

uint32_t LightController::hsvToRgb(float hue, float saturation, float value) const {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    const int i = static_cast<int>(hue * 6.0f);
    const float f = hue * 6.0f - static_cast<float>(i);
    const float p = value * (1.0f - saturation);
    const float q = value * (1.0f - f * saturation);
    const float t = value * (1.0f - (1.0f - f) * saturation);

    switch (i % 6) {
        case 0: r = value; g = t; b = p; break;
        case 1: r = q; g = value; b = p; break;
        case 2: r = p; g = value; b = t; break;
        case 3: r = p; g = q; b = value; break;
        case 4: r = t; g = p; b = value; break;
        case 5: r = value; g = p; b = q; break;
        default: break;
    }

    const uint8_t red = static_cast<uint8_t>(r * 255.0f);
    const uint8_t green = static_cast<uint8_t>(g * 255.0f);
    const uint8_t blue = static_cast<uint8_t>(b * 255.0f);
    return (static_cast<uint32_t>(red) << 16) |
           (static_cast<uint32_t>(green) << 8) |
           static_cast<uint32_t>(blue);
}

void LightController::drawSetPoint() {
    ensureUi();
    if (root == nullptr || !LvglDisplay::lock(-1)) {
        return;
    }

    const int screenW = display->width();
    const int screenH = display->height();
    const int waterLevelHeight = (waterLevel * screenH) / 100;
    const uint32_t rgb = hsvToRgb(colorHue, 1.0f, 1.0f);

    lv_obj_set_size(waterRect, screenW, waterLevelHeight);
    lv_obj_set_pos(waterRect, 0, screenH - waterLevelHeight);
    lv_obj_set_style_bg_color(waterRect, lv_color_hex(rgb), 0);

    char text[20];
    std::snprintf(text, sizeof(text), "%d%%", waterLevel);
    lv_label_set_text(levelLabel, text);

    lastWaterLevel = waterLevel;
    lastColorHue = colorHue;
    lastSetpoint = setpoint;
    LvglDisplay::invalidateScreen();
    LvglDisplay::unlock();
}

void LightController::incrementSetpoint() {
    if (waterLevel < 100) {
        waterLevel++;
        waterLevelChange = true;
        lastSetpointChangeTime = millis();
    }
}

void LightController::decrementSetpoint() {
    if (waterLevel > 0) {
        waterLevel--;
        waterLevelChange = true;
        lastSetpointChangeTime = millis();
    }
}

void LightController::setSetpoint(int value) {
    if (value >= 0 && value <= 100) {
        waterLevel = value;
        waterLevelChange = true;
        lastSetpointChangeTime = millis();
    }
}

void LightController::updateScreen() {
    ensureUi();
    if (root == nullptr) {
        return;
    }

    if (!initialized) {
        drawStaticElements();
        initialized = true;
        ESP_LOGI(TAG, "Static elements drawn");
        requestCurrentBrightnessFromServer();
    }

    const int64_t currentMillis = millis();
    checkTouchInput();

    if ((waterLevelChange || colorChange) && (currentMillis - lastSetPointDrawnTime >= 10)) {
        drawSetPoint();
        sendSetpointToServer();
        lastSetPointDrawnTime = currentMillis;
        colorChange = false;
        waterLevelChange = false;
    }

    drawModeText();
    LvglDisplay::taskHandler();
}

void LightController::checkTouchInput() {
    if (!touchPanel || display == nullptr) {
        return;
    }

    if (touchPanel->isPressed()) {
        const int touchX = touchPanel->getTouchX();
        const int touchY = touchPanel->getTouchY();
        const int screenWidth = display->width();
        const int screenHeight = display->height();

        if (touchPanel->getHasNewPress()) {
            firstTouchX = touchX;
            firstTouchY = touchY;
        }

        const int deltaX = touchX - firstTouchX;
        const int deltaY = touchY - firstTouchY;

        if (std::abs(deltaX) > 10) {
            colorHue = constrain(static_cast<float>(touchX) / static_cast<float>(screenWidth), 0.0f, 1.0f);
            colorChange = true;
        }

        if (std::abs(deltaY) > 10) {
            waterLevel = static_cast<int>((1.0f - (static_cast<float>(touchY) / screenHeight)) * 100.0f);
            waterLevel = constrain(waterLevel, 0, 100);
            waterLevelChange = true;
        }
    } else {
        firstTouchX = -1;
        firstTouchY = -1;

        if (touchPanel->getHasNewHoldRelease()) {
            if (!pageBackRequested && !colorChange && !waterLevelChange) {
                pageBackRequested = true;
                initialized = false;
                if (root != nullptr && LvglDisplay::lock(-1)) {
                    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
                    LvglDisplay::unlock();
                }
                ESP_LOGI(TAG, "Back requested");
            }
        }
    }
}

void LightController::sendSetpointToServer() {
    const bool configured = mqttConfiguredCallback ? mqttConfiguredCallback() : false;
    if (!configured) {
        ESP_LOGD(TAG, "Light MQTT not configured");
        return;
    }

    const bool connected = mqttConnectedCallback ? mqttConnectedCallback() : false;
    if (!connected || !mqttPublishCallback) {
        return;
    }

    const uint32_t rgb = hsvToRgb(colorHue, 1.0f, waterLevel / 100.0f);
    const std::string colorMessage = std::to_string(channelFromRgb(rgb, 16)) + "," +
                                     std::to_string(channelFromRgb(rgb, 8)) + "," +
                                     std::to_string(channelFromRgb(rgb, 0));
    const std::string brightnessMessage = std::to_string((waterLevel * 255) / 100);

    std::string modeMessage;
    switch (currentMode) {
        case LED_MODE_STATIC: modeMessage = "static"; break;
        case LED_MODE_GLOW: modeMessage = "glow"; break;
        case LED_MODE_RAINBOW: modeMessage = "rainbow"; break;
    }

    mqttPublishCallback("ledRing/colorControl", colorMessage);
    mqttPublishCallback("ledRing/brightnessControl", brightnessMessage);
    mqttPublishCallback("ledRing/modeControl", modeMessage);
}

void LightController::drawStaticElements() {
    ensureUi();
    if (root != nullptr && LvglDisplay::lock(-1)) {
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root);
        LvglDisplay::unlock();
    }
    drawSetPoint();
    drawModeText();
}

void LightController::requestCurrentBrightnessFromServer() {
    const bool configured = mqttConfiguredCallback ? mqttConfiguredCallback() : false;
    const bool connected = mqttConnectedCallback ? mqttConnectedCallback() : false;

    if (configured && connected && mqttPublishCallback) {
        mqttPublishCallback("esp32/light/get_brightness", "");
        ESP_LOGI(TAG, "Sent brightness request");
    } else {
        ESP_LOGD(TAG, "Light MQTT not configured, using default");
    }
}

void LightController::resetPageBackRequest() {
    pageBackRequested = false;
    initialized = false;
}

void LightController::drawModeText() {
    ensureUi();
    if (root == nullptr || !LvglDisplay::lock(-1)) {
        return;
    }

    const char* modeText = "Static";
    switch (currentMode) {
        case LED_MODE_STATIC: modeText = "Static"; break;
        case LED_MODE_GLOW: modeText = "Glow"; break;
        case LED_MODE_RAINBOW: modeText = "Rainbow"; break;
    }

    lv_label_set_text(modeLabel, modeText);
    LvglDisplay::unlock();
}

void LightController::handleModeChange() {
    if (modeChangeRequested) {
        currentMode = static_cast<LedMode>((currentMode + 1) % 3);
        modeChangeRequested = false;
        drawModeText();
        sendSetpointToServer();
    }
}

void LightController::onBrightnessReceived(int brightness) {
    if (brightness >= 0 && brightness <= 100) {
        waterLevel = brightness;
        waterLevelChange = true;
        ESP_LOGI(TAG, "Brightness received: %d", brightness);
    }
}

int LightController::scalePx(int referencePx) const {
    return std::max(1, (referencePx * display->width()) / 240);
}
