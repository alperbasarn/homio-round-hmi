#pragma once

#include "DisplayPanelBackend.h"
#include "input/InputEvent.h"
#include "esp_timer.h"
#include "model/MediaModel.h"
#include "model/MutationQueue.h"
#include <cstdint>
#include <functional>
#include <lvgl.h>
#include <string>

enum ControllerState {
    CONTROLLER_INITIALIZE,
    CONTROLLER_WAIT_FOR_SYNC,
    CONTROLLER_UPDATE
};

using MQTTPublishCallback = std::function<void(const std::string& topic, const std::string& message)>;
using MQTTConnectedCallback = std::function<bool()>;

class SoundController {
private:
    DisplayPanelBackend* display;

    int setpoint;
    int lastSetpoint;
    int lastDrawnSetpoint;
    int lastSentSetpoint;
    bool externalSetpointChange;
    bool isPlaying;
    bool colorChange;
    bool initialized;
    bool mqttInitialized;
    bool stateReceived;
    bool pageBackRequested;
    bool leaving;
    ControllerState controllerState;
    int64_t lastSentTime;
    int64_t lastSetpointChangeTime;
    int64_t lastSetpointSentTime;

    lv_obj_t* root;
    lv_obj_t* volumeArc;
    lv_obj_t* volumeLabel;
    lv_obj_t* stateLabel;
    lv_obj_t* hintLabel;

    MQTTPublishCallback mqttPublishCallback;
    MQTTConnectedCallback mqttConnectedCallback;

    std::string pendingMessage;
    bool hasPendingMessage;
    model::MutationQueue* modelQueue;   // CTRL-07: volume and play state also go to MediaModel as records

    void postMedia(model::MediaModel::Field field, int value);
    void ensureUi();
    void initializeController();
    void waitForSync();
    void updateController();
    void drawSetPoint();
    void sendSetpointToServer();
    void requestInitialStateFromServer();
    void updateMediaButtons();
    void handleMediaButtonEvents();
    void processIncomingMessage();
    void resetController();
    int scalePx(int referencePx) const;

    int64_t millis() const { return esp_timer_get_time() / 1000; }

public:
    explicit SoundController(DisplayPanelBackend* displayBackend);
    // INPT-18 / CTRL-10: a tap toggles play/pause, a 3 s hold leaves; the knob arrives through
    // DisplayController::applyKnobDelta.
    void onInput(const input::InputEvent& event);

    void updateScreen();
    void updateSetpoint(int setpoint);
    void incrementSetpoint();
    void decrementSetpoint();
    void togglePlayPause();
    int getSetpoint() const { return setpoint; }

    bool isPageBackRequested() const { return pageBackRequested; }
    void resetPageBackRequest();

    void setMQTTPublishCallback(MQTTPublishCallback callback) { mqttPublishCallback = callback; }
    void setMQTTConnectedCallback(MQTTConnectedCallback callback) { mqttConnectedCallback = callback; }
    void setModelQueue(model::MutationQueue* queue) { modelQueue = queue; }

    void onMQTTMessage(const std::string& message);
};
