#pragma once

#include "LvglDisplay.h"
#include "TouchPanel.h"
#include "Buttons.h"
#include "Arc.h"
#include "esp_timer.h"
#include <cstdint>
#include <string>
#include <functional>

// State machine enum
enum ControllerState {
    CONTROLLER_INITIALIZE,
    CONTROLLER_WAIT_FOR_SYNC,
    CONTROLLER_UPDATE
};

// Callback types for MQTT communication
using MQTTPublishCallback = std::function<void(const std::string& topic, const std::string& message)>;
using MQTTConnectedCallback = std::function<bool()>;

class SoundController {
private:
    TouchPanel* touchPanel;

    // Volume and state tracking
    int setpoint;
    int lastSetpoint;
    int lastDrawnSetpoint;
    int lastSentSetpoint;
    bool externalSetpointChange;
    bool isPlaying;

    // UI state tracking
    bool colorChange;
    bool initialized;
    bool mqttInitialized;
    bool stateReceived;
    bool pageBackRequested;

    // State machine
    ControllerState controllerState;

    // Time management
    int64_t lastSentTime;
    int64_t lastSetpointChangeTime;
    int64_t lastSetpointSentTime;

    // UI elements
    PlayButton playButton;
    PauseButton pauseButton;
    RewindButton rewindButton;
    ForwardButton forwardButton;
    BackButton backButton;
    Arc volumeArc;

    // MQTT callbacks
    MQTTPublishCallback mqttPublishCallback;
    MQTTConnectedCallback mqttConnectedCallback;

    // Pending message for processing
    std::string pendingMessage;
    bool hasPendingMessage;

    // State machine methods
    void initializeController();
    void waitForSync();
    void updateController();

    // Internal methods
    void drawStaticVolumeElements();
    void drawSetPoint();
    void checkTouchInput();
    void sendSetpointToServer();
    void requestInitialStateFromServer();
    void drawMediaButtons();
    void updateMediaButtons();
    void handleMediaButtonEvents();
    void processIncomingMessage();
    void resetController();

    // Helper
    int64_t millis() const { return esp_timer_get_time() / 1000; }

public:
    explicit SoundController(TouchPanel* touch);

    // Main update method
    void updateScreen();

    // Volume control methods
    void updateSetpoint(int setpoint);
    void incrementSetpoint();
    void decrementSetpoint();

    // Media control methods
    void togglePlayPause();

    // Navigation methods
    bool isPageBackRequested() const { return pageBackRequested; }
    void resetPageBackRequest();

    // MQTT callbacks
    void setMQTTPublishCallback(MQTTPublishCallback callback) { mqttPublishCallback = callback; }
    void setMQTTConnectedCallback(MQTTConnectedCallback callback) { mqttConnectedCallback = callback; }

    // Process incoming MQTT message
    void onMQTTMessage(const std::string& message);
};
