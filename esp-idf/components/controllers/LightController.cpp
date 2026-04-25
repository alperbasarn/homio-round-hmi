#include "LightController.h"
#include "esp_log.h"
#include <cmath>
#include <algorithm>

static const char* TAG = "LightCtrl";

LightController::LightController(TouchPanel* touch)
    : touchPanel(touch),
      setpoint(50), lastSetpoint(-1), lastSentSetpoint(-1), lastSentColor(0),
      lastSetpointChangeTime(0), lastColorChangeTime(0), lastWaterLevelSentTime(0),
      lastSetPointDrawnTime(0), lastScreenUpdateTime(0), lastSetpointQueryTime(0),
      waterLevel(50), lastWaterLevel(-1),
      colorHue(0.5f), lastColorHue(-1),
      colorChange(false), initialized(false), pageBackRequested(false),
      waterLevelChange(false),
      firstTouchX(-1), firstTouchY(-1),
      currentMode(LED_MODE_STATIC), modeChangeRequested(false) {
}

uint16_t LightController::hsvToRgb565(float hue, float saturation, float value) {
    float r, g, b;
    int i = static_cast<int>(hue * 6);
    float f = hue * 6 - i;
    float p = value * (1 - saturation);
    float q = value * (1 - f * saturation);
    float t = value * (1 - (1 - f) * saturation);

    switch (i % 6) {
        case 0: r = value; g = t; b = p; break;
        case 1: r = q; g = value; b = p; break;
        case 2: r = p; g = value; b = t; break;
        case 3: r = p; g = q; b = value; break;
        case 4: r = t; g = p; b = value; break;
        case 5: r = value; g = p; b = q; break;
        default: r = g = b = 0; break;
    }

    uint8_t red   = static_cast<uint8_t>(r * 255);
    uint8_t green = static_cast<uint8_t>(g * 255);
    uint8_t blue  = static_cast<uint8_t>(b * 255);

    return LvglDisplay::color565(red, green, blue);
}

void LightController::drawSetPoint() {
    const int h = LvglDisplay::getHeight();
    const int w = LvglDisplay::getWidth();
    const int wlh = (waterLevel * h) / 100;
    const uint16_t color565 = hsvToRgb565(colorHue, 1.0f, 1.0f);
    const uint32_t color888 = ((static_cast<uint32_t>((color565 >> 11) & 0x1F) * 255 / 31) << 16)
                            | ((static_cast<uint32_t>((color565 >>  5) & 0x3F) * 255 / 63) <<  8)
                            |  (static_cast<uint32_t>( color565        & 0x1F) * 255 / 31);
    const int lastWlh = (lastWaterLevel * h) / 100;
    const int clearH = std::abs(wlh - lastWlh);
    const int clearY = h - std::max(wlh, lastWlh);

    LvglDisplay::fillRect(0, clearY, w, clearH, 0x000000);
    LvglDisplay::fillRect(0, h - wlh, w, wlh, color888);

    lastWaterLevel = waterLevel;
    lastColorHue   = colorHue;
    lastSetpoint   = setpoint;
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
    if (!initialized) {
        drawStaticElements();
        initialized = true;
        ESP_LOGI(TAG, "Static elements drawn");
        requestCurrentBrightnessFromServer();
    } else {
        int64_t currentMillis = millis();

        checkTouchInput();

        if ((waterLevelChange || colorChange) && (currentMillis - lastSetPointDrawnTime >= 10)) {
            drawSetPoint();
            sendSetpointToServer();
            lastSetPointDrawnTime = currentMillis;
            colorChange = false;
            waterLevelChange = false;
        }

        drawModeText();
    }
}

void LightController::checkTouchInput() {
    if (!touchPanel) return;

    if (touchPanel->isPressed()) {
        int touchX = touchPanel->getTouchX();
        int touchY = touchPanel->getTouchY();
        int screenWidth  = LvglDisplay::getWidth();
        int screenHeight = LvglDisplay::getHeight();

        if (touchPanel->getHasNewPress()) {
            firstTouchX = touchX;
            firstTouchY = touchY;
        }

        int deltaX = touchX - firstTouchX;
        int deltaY = touchY - firstTouchY;

        if (std::abs(deltaX) > 10 || std::abs(deltaY) > 10) {
            if (std::abs(deltaX) > 10) {
                colorHue = static_cast<float>(touchX) / screenWidth;
                colorChange = true;
            }

            if (std::abs(deltaY) > 10) {
                waterLevel = static_cast<int>((1.0f - (static_cast<float>(touchY) / screenHeight)) * 100);
                waterLevel = constrain(waterLevel, 0, 100);
                waterLevelChange = true;
            }
        }
    } else {
        firstTouchX = -1;
        firstTouchY = -1;

        if (touchPanel->getHasNewHoldRelease()) {
            if (!pageBackRequested && !colorChange && !waterLevelChange) {
                pageBackRequested = true;
                ESP_LOGI(TAG, "Back requested");
            }
        }
    }
}

void LightController::sendSetpointToServer() {
    bool configured = mqttConfiguredCallback ? mqttConfiguredCallback() : false;
    if (!configured) {
        ESP_LOGD(TAG, "Light MQTT not configured");
        return;
    }

    bool connected = mqttConnectedCallback ? mqttConnectedCallback() : false;
    if (!connected || !mqttPublishCallback) {
        return;
    }

    uint16_t color = hsvToRgb565(colorHue, 1.0f, 1.0f);
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;

    r = static_cast<uint8_t>((r * 255 / 31) * (waterLevel / 100.0f));
    g = static_cast<uint8_t>((g * 255 / 63) * (waterLevel / 100.0f));
    b = static_cast<uint8_t>((b * 255 / 31) * (waterLevel / 100.0f));

    std::string colorMessage = std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b);
    std::string brightnessMessage = std::to_string((waterLevel * 255) / 100);

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
    LvglDisplay::fillScreen(0x000000);
    drawSetPoint();
}

void LightController::requestCurrentBrightnessFromServer() {
    bool configured = mqttConfiguredCallback ? mqttConfiguredCallback() : false;
    bool connected = mqttConnectedCallback ? mqttConnectedCallback() : false;

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
    const int sw = LvglDisplay::getWidth();
    const int sh = LvglDisplay::getHeight();
    const int textScale = std::max(1, sw / 120);
    LvglDisplay::setTextSize(textScale);
    LvglDisplay::setTextColor(0xFFFFFF);

    const char* modeText;
    switch (currentMode) {
        case LED_MODE_STATIC:  modeText = "Static";  break;
        case LED_MODE_GLOW:    modeText = "Glow";    break;
        case LED_MODE_RAINBOW: modeText = "Rainbow"; break;
        default:               modeText = "Unknown"; break;
    }

    int tw = LvglDisplay::textWidth(modeText);
    LvglDisplay::setCursor((sw - tw) / 2, sh - 30 * sw / 240);
    LvglDisplay::print(modeText);

    const int triSize = 20 * sw / 240;
    const int triY    = sh - 20 * sw / 240;
    LvglDisplay::fillTriangle(10, triY, 10 + triSize, triY - triSize, 10 + triSize, triY + triSize/2, 0xFFFFFF);
    LvglDisplay::fillTriangle(sw - 10, triY, sw - 10 - triSize, triY - triSize, sw - 10 - triSize, triY + triSize/2, 0xFFFFFF);
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
