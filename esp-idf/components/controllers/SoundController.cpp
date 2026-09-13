#include "SoundController.h"
#include "LvglDisplay.h"
#include "esp_log.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>

namespace {

constexpr const char* TAG = "SoundCtrl";

}  // namespace

SoundController::SoundController(DisplayPanelBackend* displayBackend)
    : display(displayBackend),
      setpoint(50),
      lastSetpoint(-1),
      lastDrawnSetpoint(-1),
      lastSentSetpoint(-1),
      externalSetpointChange(false),
      isPlaying(false),
      colorChange(false),
      initialized(false),
      mqttInitialized(false),
      stateReceived(false),
      pageBackRequested(false),
      leaving(false),
      controllerState(CONTROLLER_INITIALIZE),
      lastSentTime(0),
      lastSetpointChangeTime(0),
      lastSetpointSentTime(0),
      root(nullptr),
      volumeArc(nullptr),
      volumeLabel(nullptr),
      stateLabel(nullptr),
      hintLabel(nullptr),
      hasPendingMessage(false),
      modelQueue(nullptr) {
}

void SoundController::postMedia(model::MediaModel::Field field, int value) {
    if (modelQueue == nullptr) return;
    modelQueue->post(model::MediaModel::make(field, value, static_cast<uint32_t>(millis())));
}

void SoundController::ensureUi() {
    if (root != nullptr || display == nullptr || !LvglDisplay::isInitialized()) {
        return;
    }

    if (!LvglDisplay::lock(-1)) {
        ESP_LOGW(TAG, "LVGL lock unavailable while creating sound controller UI");
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

    const int diameter = display->width() - scalePx(54);
    volumeArc = lv_arc_create(root);
    lv_obj_remove_style_all(volumeArc);
    lv_obj_set_size(volumeArc, diameter, diameter);
    lv_obj_center(volumeArc);
    lv_obj_set_style_arc_width(volumeArc, scalePx(16), LV_PART_MAIN);
    lv_obj_set_style_arc_width(volumeArc, scalePx(16), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(volumeArc, lv_color_hex(0x122238), LV_PART_MAIN);
    lv_obj_set_style_arc_color(volumeArc, lv_color_hex(0x39D9FF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(volumeArc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(volumeArc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(volumeArc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(volumeArc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(volumeArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_arc_set_rotation(volumeArc, 180);
    lv_arc_set_bg_angles(volumeArc, 0, 180);
    lv_arc_set_range(volumeArc, 0, 100);

    volumeLabel = lv_label_create(root);
    lv_obj_set_style_text_color(volumeLabel, lv_color_white(), 0);
    lv_obj_align(volumeLabel, LV_ALIGN_CENTER, 0, -scalePx(10));

    stateLabel = lv_label_create(root);
    lv_obj_set_style_text_color(stateLabel, lv_color_hex(0xB8C5D8), 0);
    lv_obj_align(stateLabel, LV_ALIGN_CENTER, 0, scalePx(26));

    hintLabel = lv_label_create(root);
    lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x6B778A), 0);
    lv_label_set_text(hintLabel, "Tap play/pause  •  Hold 3 s: back");
    lv_obj_align(hintLabel, LV_ALIGN_BOTTOM_MID, 0, -scalePx(18));

    LvglDisplay::unlock();
}

void SoundController::updateScreen() {
    if (leaving) {
        resetController();
        return;
    }

    switch (controllerState) {
        case CONTROLLER_INITIALIZE:
            initializeController();
            controllerState = CONTROLLER_WAIT_FOR_SYNC;
            break;
        case CONTROLLER_WAIT_FOR_SYNC:
            waitForSync();
            if (stateReceived) {
                ESP_LOGI(TAG, "State received, moving to UPDATE state");
                controllerState = CONTROLLER_UPDATE;
            }
            break;
        case CONTROLLER_UPDATE:
            updateController();
            break;
    }

    LvglDisplay::taskHandler();
}

void SoundController::initializeController() {
    ensureUi();
    if (root == nullptr || !LvglDisplay::lock(-1)) {
        return;
    }

    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
    drawSetPoint();
    LvglDisplay::invalidateScreen();
    LvglDisplay::unlock();

    ESP_LOGI(TAG, "Controller initialized");
    initialized = true;
}

void SoundController::waitForSync() {
    const bool mqttConnected = mqttConnectedCallback ? mqttConnectedCallback() : false;

    if (mqttConnected && !mqttInitialized) {
        requestInitialStateFromServer();
        mqttInitialized = true;
        ESP_LOGI(TAG, "MQTT connected, sent initial state request");
    }

    updateMediaButtons();
    processIncomingMessage();
}

void SoundController::updateController() {
    if (setpoint != lastSetpoint) {
        drawSetPoint();
        lastSetpoint = setpoint;
    }

    updateMediaButtons();
    handleMediaButtonEvents();
    processIncomingMessage();

    const int64_t currentMillis = millis();
    if ((setpoint != lastSentSetpoint) &&
        !externalSetpointChange &&
        (currentMillis - lastSetpointSentTime >= 25)) {
        sendSetpointToServer();
        lastSetpointSentTime = currentMillis;
        lastSentSetpoint = setpoint;
        ESP_LOGI(TAG, "Sent locally changed setpoint: %d", setpoint);
    }

    if (externalSetpointChange) {
        externalSetpointChange = false;
    }
}

void SoundController::drawSetPoint() {
    ensureUi();
    if (root == nullptr || !LvglDisplay::lock(-1)) {
        return;
    }

    lv_arc_set_value(volumeArc, setpoint);

    char text[20];
    std::snprintf(text, sizeof(text), "%d%%", setpoint);
    lv_label_set_text(volumeLabel, text);
    lv_label_set_text(stateLabel, isPlaying ? "Playing" : "Paused");
    LvglDisplay::invalidateScreen();
    LvglDisplay::unlock();
}

void SoundController::processIncomingMessage() {
    if (!hasPendingMessage) return;
    hasPendingMessage = false;

    ESP_LOGI(TAG, "Processing message: %s", pendingMessage.c_str());

    if (pendingMessage.find("setpoint:") == 0) {
        const int newSetpoint = std::atoi(pendingMessage.c_str() + 9);
        if (newSetpoint >= 0 && newSetpoint <= 100 && newSetpoint != setpoint) {
            externalSetpointChange = true;
            setpoint = newSetpoint;
            lastSetpointChangeTime = millis();
            lastSentSetpoint = setpoint;
            drawSetPoint();
            postMedia(model::MediaModel::Field::Volume, setpoint);
        }
    } else if (pendingMessage.find("state:") == 0) {
        const size_t commaPos = pendingMessage.find(',');
        if (commaPos != std::string::npos) {
            const std::string playState = pendingMessage.substr(6, commaPos - 6);
            isPlaying = (playState == "playing");
            postMedia(model::MediaModel::Field::IsPlaying, isPlaying ? 1 : 0);

            const size_t volPos = pendingMessage.find("volume:");
            if (volPos != std::string::npos) {
                const int newVol = std::atoi(pendingMessage.c_str() + volPos + 7);
                if (newVol >= 0 && newVol <= 100) {
                    setpoint = newVol;
                    lastSentSetpoint = setpoint;
                    postMedia(model::MediaModel::Field::Volume, setpoint);
                }
            }

            stateReceived = true;
            drawSetPoint();
        }
    } else if (pendingMessage == "unpaused" || pendingMessage == "playing") {
        isPlaying = true;
        if (!stateReceived) stateReceived = true;
        drawSetPoint();
        postMedia(model::MediaModel::Field::IsPlaying, 1);
    } else if (pendingMessage == "paused") {
        isPlaying = false;
        if (!stateReceived) stateReceived = true;
        drawSetPoint();
        postMedia(model::MediaModel::Field::IsPlaying, 0);
    } else {
        // CTRL-01: this branch used to be silent, which is how the dead PC round-trip went unnoticed.
        ESP_LOGW(TAG, "Inbound sound message matched no branch: '%s'", pendingMessage.c_str());
    }
}

void SoundController::resetController() {
    pageBackRequested = true;
    initialized = false;
    mqttInitialized = false;
    stateReceived = false;
    leaving = false;
    controllerState = CONTROLLER_INITIALIZE;
    if (root != nullptr && LvglDisplay::lock(-1)) {
        lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
        LvglDisplay::unlock();
    }
    ESP_LOGI(TAG, "Controller reset");
}

void SoundController::handleMediaButtonEvents() {
}

void SoundController::updateMediaButtons() {
}

void SoundController::onInput(const input::InputEvent& event) {
    switch (event.type) {
        case input::InputType::TouchRelease:
            togglePlayPause();
            break;
        case input::InputType::TouchHoldRelease:
            leaving = true;  // D13: the router only emits this at 3 s or more
            break;
        default:
            break;
    }
}

void SoundController::incrementSetpoint() {
    if (setpoint < 100) {
        setpoint++;
        lastSetpointChangeTime = millis();
        externalSetpointChange = false;
        drawSetPoint();
        postMedia(model::MediaModel::Field::VolumeLocal, setpoint);
    }
}

void SoundController::decrementSetpoint() {
    if (setpoint > 0) {
        setpoint--;
        lastSetpointChangeTime = millis();
        externalSetpointChange = false;
        drawSetPoint();
        postMedia(model::MediaModel::Field::VolumeLocal, setpoint);
    }
}

void SoundController::sendSetpointToServer() {
    if (mqttConnectedCallback && mqttConnectedCallback() && mqttPublishCallback) {
        const std::string msg = "setpoint:" + std::to_string(setpoint);
        mqttPublishCallback("esp32/sound/setpoint", msg);
        ESP_LOGI(TAG, "Sound setpoint sent: %d", setpoint);
    }
}

void SoundController::requestInitialStateFromServer() {
    if (mqttConnectedCallback && mqttConnectedCallback() && mqttPublishCallback) {
        mqttPublishCallback("esp32/sound/get_state", "request");
        ESP_LOGI(TAG, "Sent get_state request");
    }
}

void SoundController::updateSetpoint(int level) {
    if (level != setpoint) {
        setpoint = level;
        lastSetpointChangeTime = millis();
        externalSetpointChange = false;
        drawSetPoint();
        postMedia(model::MediaModel::Field::VolumeLocal, setpoint);
    }
}

void SoundController::resetPageBackRequest() {
    pageBackRequested = false;
    leaving = false;
    controllerState = CONTROLLER_INITIALIZE;
    initialized = false;
    mqttInitialized = false;
    stateReceived = false;
    ESP_LOGI(TAG, "Page back request reset");
}

void SoundController::onMQTTMessage(const std::string& message) {
    pendingMessage = message;
    hasPendingMessage = true;
}

void SoundController::togglePlayPause() {
    const bool mqttConnected = mqttConnectedCallback ? mqttConnectedCallback() : false;
    if (mqttConnected && mqttPublishCallback) {
        mqttPublishCallback("esp32/sound/control", isPlaying ? "pause" : "play");
    }
}

int SoundController::scalePx(int referencePx) const {
    return std::max(1, (referencePx * display->width()) / 240);
}
