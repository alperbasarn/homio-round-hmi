#include "InitializationScreen.h"
#include "LvglDisplay.h"
#include "esp_log.h"
#include <cstdio>

namespace {

constexpr const char* TAG = "InitScreen";

}  // namespace

InitializationScreen::InitializationScreen(DisplayPanelBackend* displayBackend)
    : display(displayBackend),
      progressValue(0),
      previousProgress(0),
      screenInitialized(false),
      wifiConnected(false),
      wifiStrength(0),
      mqttConnected(false),
      root(nullptr),
      progressArc(nullptr),
      logoLabel(nullptr),
      progressLabel(nullptr),
      statusLabel(nullptr) {
}

void InitializationScreen::ensureUi() {
    if (screenInitialized || display == nullptr || !LvglDisplay::isInitialized()) {
        return;
    }

    if (!LvglDisplay::lock(-1)) {
        ESP_LOGW(TAG, "LVGL lock unavailable while creating initialization screen");
        return;
    }

    const int displayW = display->width();
    const int displayH = display->height();
    const int displaySize = std::min(displayW, displayH);
    const int arcDiameter = displaySize - std::max(scalePx(28), displaySize / 8);
    const int arcWidth = std::max(scalePx(14), displaySize / 24);

    root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, displayW, displayH);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    progressArc = lv_arc_create(root);
    lv_obj_remove_style_all(progressArc);
    lv_obj_set_size(progressArc, arcDiameter, arcDiameter);
    lv_obj_center(progressArc);
    lv_obj_set_style_arc_width(progressArc, arcWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_width(progressArc, arcWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(progressArc, lv_color_hex(0x121A2A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(progressArc, lv_color_hex(0xFF4858), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(progressArc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(progressArc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(progressArc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(progressArc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progressArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(progressArc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(progressArc, 120);
    lv_arc_set_bg_angles(progressArc, 0, 300);
    lv_arc_set_range(progressArc, 0, 100);
    lv_arc_set_value(progressArc, progressValue);

    logoLabel = lv_label_create(root);
    lv_obj_set_style_text_color(logoLabel, lv_color_white(), 0);
    lv_label_set_text(logoLabel, "QNOB");
    lv_obj_align(logoLabel, LV_ALIGN_CENTER, 0, -scalePx(12));

    progressLabel = lv_label_create(root);
    lv_obj_set_style_text_color(progressLabel, lv_color_hex(0xAEB8C6), 0);
    lv_obj_align(progressLabel, LV_ALIGN_CENTER, 0, scalePx(24));

    statusLabel = lv_label_create(root);
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x647086), 0);
    lv_obj_align(statusLabel, LV_ALIGN_CENTER, 0, scalePx(48));

    screenInitialized = true;
    updateWidgets();
    lv_obj_move_foreground(root);
    LvglDisplay::invalidateScreen();
    LvglDisplay::unlock();
}

void InitializationScreen::updateWidgets() {
    if (progressArc == nullptr || progressLabel == nullptr || statusLabel == nullptr) {
        return;
    }

    lv_arc_set_value(progressArc, progressValue);

    char progressText[16];
    std::snprintf(progressText, sizeof(progressText), "%d%%", progressValue);
    lv_label_set_text(progressLabel, progressText);

    const char* wifiText = wifiConnected ? "WiFi ready" : "Starting";
    const char* mqttText = mqttConnected ? "MQTT ready" : "MQTT pending";
    char statusText[48];
    std::snprintf(statusText, sizeof(statusText), "%s | %s", wifiText, mqttText);
    lv_label_set_text(statusLabel, statusText);
}

void InitializationScreen::setProgress(int progress) {
    previousProgress = progressValue;
    progressValue = constrain(progress, 0, 100);
    ESP_LOGI(TAG, "Progress updated: %d", progressValue);
    updateScreen();
}

void InitializationScreen::setMQTTStatus(bool isConnected) {
    mqttConnected = isConnected;
    updateScreen();
}

void InitializationScreen::updateScreen() {
    ensureUi();
    if (!screenInitialized) {
        return;
    }

    if (!LvglDisplay::lock(-1)) {
        return;
    }
    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
    updateWidgets();
    LvglDisplay::invalidateScreen();
    LvglDisplay::unlock();
    LvglDisplay::taskHandler();
}

void InitializationScreen::setWiFiStatus(bool connected, int strength) {
    wifiConnected = connected;
    wifiStrength = constrain(strength, 0, 3);
    updateScreen();
}

void InitializationScreen::reset() {
    progressValue = 0;
    previousProgress = 0;
    wifiConnected = false;
    wifiStrength = 0;
    mqttConnected = false;

    if (screenInitialized && LvglDisplay::lock(-1)) {
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root);
        updateWidgets();
        LvglDisplay::invalidateScreen();
        LvglDisplay::unlock();
    }
}

int InitializationScreen::scalePx(int referencePx) const {
    if (display == nullptr) {
        return referencePx;
    }
    return std::max(1, (referencePx * display->width()) / 240);
}
