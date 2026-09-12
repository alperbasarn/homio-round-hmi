#include "LightController.h"
#include "LvglDisplay.h"
#include "esp_log.h"
#include "model/ColorMath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr const char* TAG = "LightCtrl";

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
      publishPending(false),
      firstTouchX(-1),
      firstTouchY(-1),
      currentMode(LED_MODE_STATIC),
      modeChangeRequested(false),
      root(nullptr),
      waterRect(nullptr),
      levelLabel(nullptr),
      modeLabel(nullptr),
      hintLabel(nullptr),
      modelQueue(nullptr) {
}

void LightController::postBrightness() {
    if (modelQueue == nullptr) return;
    modelQueue->post(model::LightModel::make(model::LightModel::Field::Brightness, waterLevel,
                                             static_cast<uint32_t>(millis())));
}

void LightController::postHue() {
    if (modelQueue == nullptr) return;
    modelQueue->post(model::LightModel::makeFloat(model::LightModel::Field::Hue, colorHue,
                                                  static_cast<uint32_t>(millis())));
}

void LightController::postMode() {
    if (modelQueue == nullptr) return;
    // LED_MODE_STATIC/GLOW/RAINBOW share the model's LedMode numbering.
    modelQueue->post(model::LightModel::make(model::LightModel::Field::Mode, static_cast<int32_t>(currentMode),
                                             static_cast<uint32_t>(millis())));
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

void LightController::drawSetPoint() {
    ensureUi();
    if (root == nullptr || !LvglDisplay::lock(-1)) {
        return;
    }

    const int screenW = display->width();
    const int screenH = display->height();
    const int waterLevelHeight = (waterLevel * screenH) / 100;
    const uint32_t rgb = model::hsvToRgb(colorHue, 1.0f, 1.0f);  // MDL-09: swatch at full value

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
        postBrightness();
    }
}

void LightController::decrementSetpoint() {
    if (waterLevel > 0) {
        waterLevel--;
        waterLevelChange = true;
        lastSetpointChangeTime = millis();
        postBrightness();
    }
}

void LightController::setSetpoint(int value) {
    if (value >= 0 && value <= 100) {
        waterLevel = value;
        waterLevelChange = true;
        lastSetpointChangeTime = millis();
        postBrightness();
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
        publishPending = true;
        lastSetPointDrawnTime = currentMillis;
        colorChange = false;
        waterLevelChange = false;
    }

    // CTRL-04: a drag redraws up to 100 times a second and each publish is three topics, so the
    // unthrottled path pushed ~300 msg/s. Publish at most every kPublishIntervalMs and always
    // flush the last value: publishPending stays set until a slot opens.
    if (publishPending && (currentMillis - lastWaterLevelSentTime >= kPublishIntervalMs)) {
        sendSetpointToServer();
        lastWaterLevelSentTime = currentMillis;
        publishPending = false;
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
            postHue();
        }

        if (std::abs(deltaY) > 10) {
            waterLevel = static_cast<int>((1.0f - (static_cast<float>(touchY) / screenHeight)) * 100.0f);
            waterLevel = constrain(waterLevel, 0, 100);
            waterLevelChange = true;
            postBrightness();
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
    // CTRL-01: both gates used to drop the publish silently; the configured gate was permanently
    // closed until CTRL-03. Logged at most every 2 s so a drag while offline does not flood.
    static int64_t lastGateLogMs = 0;
    const bool configured = mqttConfiguredCallback ? mqttConfiguredCallback() : false;
    const bool connected = mqttConnectedCallback ? mqttConnectedCallback() : false;
    if (!configured || !connected || !mqttPublishCallback) {
        const int64_t now = millis();
        if (now - lastGateLogMs >= 2000) {
            lastGateLogMs = now;
            ESP_LOGW(TAG, "light gate closed: %s, publish dropped",
                     !configured ? "MQTT not configured" : "MQTT not connected");
        }
        return;
    }

    // MDL-09: colour maths lives in the model now (bit-exact port of the former member).
    const uint32_t rgb = model::hsvToRgb(colorHue, 1.0f, waterLevel / 100.0f);
    const std::string colorMessage = std::to_string(model::channelFromRgb(rgb, 16)) + "," +
                                     std::to_string(model::channelFromRgb(rgb, 8)) + "," +
                                     std::to_string(model::channelFromRgb(rgb, 0));
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
        postMode();
    }
}

void LightController::onBrightnessReceived(int brightness) {
    if (brightness >= 0 && brightness <= 100) {
        waterLevel = brightness;
        waterLevelChange = true;
        ESP_LOGI(TAG, "Brightness received: %d", brightness);
        postBrightness();
    }
}

int LightController::scalePx(int referencePx) const {
    return std::max(1, (referencePx * display->width()) / 240);
}
