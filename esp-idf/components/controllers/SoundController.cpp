#include "SoundController.h"
#include "esp_log.h"
#include <cstring>
#include <cstdlib>

static const char* TAG = "SoundCtrl";

SoundController::SoundController(TouchPanel* touch)
    : touchPanel(touch),
      setpoint(50), lastSetpoint(-1), lastDrawnSetpoint(-1), lastSentSetpoint(-1),
      externalSetpointChange(false), isPlaying(false),
      colorChange(false), initialized(false), mqttInitialized(false),
      stateReceived(false), pageBackRequested(false),
      controllerState(CONTROLLER_INITIALIZE),
      lastSentTime(0), lastSetpointChangeTime(0), lastSetpointSentTime(0),
      playButton(touch), pauseButton(touch),
      rewindButton(touch), forwardButton(touch),
      backButton(touch), volumeArc(),
      hasPendingMessage(false) {
}

void SoundController::updateScreen() {
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
                volumeArc.stopSegmentAnimation();
            }
            break;
        case CONTROLLER_UPDATE:
            updateController();
            break;
    }

    // Always update the volume arc animation
    volumeArc.update();

    // Always process touch input for back button
    if (backButton.getHasNewRelease()) {
        resetController();
    }
}

void SoundController::initializeController() {
    LvglDisplay::fillScreen(0x000000);

    ESP_LOGI(TAG, "Drawing UI elements in INITIALIZE state");

    float scale = static_cast<float>(LvglDisplay::getWidth()) / 240.0f;
    int buttonSize = static_cast<int>(60 * scale);
    int centerX = LvglDisplay::getWidth() / 2;
    int centerY = LvglDisplay::getHeight() / 2;
    int spacing = static_cast<int>(60 * scale);

    // Initialize buttons
    playButton.initialize(centerX, centerY, buttonSize);
    pauseButton.initialize(centerX, centerY, buttonSize);
    rewindButton.initialize(centerX - spacing, centerY, buttonSize);
    forwardButton.initialize(centerX + spacing, centerY, buttonSize);

    // Initialize back button
    int backWidth = static_cast<int>(40 * scale);
    int backHeight = static_cast<int>(20 * scale);
    backButton.initialize(centerX, LvglDisplay::getHeight() - static_cast<int>(30 * scale), backWidth, backHeight);

    // Initialize volume arc
    int arcRadius = static_cast<int>(110 * scale);
    int arcThickness = static_cast<int>(15 * scale);
    volumeArc.initialize(centerX, centerY, arcRadius, arcThickness, 180, 180);
    volumeArc.setColor(0x00FFFF);  // Cyan (RGB888)

    // Hide both play/pause initially
    playButton.hide();
    pauseButton.hide();

    // Draw buttons
    forwardButton.draw();
    rewindButton.draw();
    backButton.draw();

    // Show correct button based on state
    if (isPlaying) {
        pauseButton.unhide();
        pauseButton.draw();
    } else {
        playButton.unhide();
        playButton.draw();
    }

    // Start segment animation until MQTT sync
    volumeArc.startSegmentAnimation();

    ESP_LOGI(TAG, "Controller initialized");
    initialized = true;
}

void SoundController::waitForSync() {
    bool mqttConnected = mqttConnectedCallback ? mqttConnectedCallback() : false;

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
        volumeArc.setPercentage(setpoint);
        lastSetpoint = setpoint;
    }

    updateMediaButtons();
    handleMediaButtonEvents();
    processIncomingMessage();
    checkTouchInput();

    int64_t currentMillis = millis();
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

void SoundController::processIncomingMessage() {
    if (!hasPendingMessage) return;
    hasPendingMessage = false;

    ESP_LOGI(TAG, "Processing message: %s", pendingMessage.c_str());

    // Parse setpoint
    if (pendingMessage.find("setpoint:") == 0) {
        int newSetpoint = std::atoi(pendingMessage.c_str() + 9);
        if (newSetpoint >= 0 && newSetpoint <= 100 && newSetpoint != setpoint) {
            externalSetpointChange = true;
            setpoint = newSetpoint;
            lastSetpointChangeTime = millis();
            volumeArc.setPercentage(setpoint);
            lastSentSetpoint = setpoint;

            if (volumeArc.isSegmentAnimationActive()) {
                volumeArc.stopSegmentAnimation();
            }

            ESP_LOGI(TAG, "Setpoint updated from MQTT: %d", setpoint);
        }
    }
    // Parse state response
    else if (pendingMessage.find("state:") == 0) {
        size_t commaPos = pendingMessage.find(',');
        if (commaPos != std::string::npos) {
            std::string playState = pendingMessage.substr(6, commaPos - 6);

            if (playState == "playing") {
                isPlaying = true;
                playButton.hide();
                pauseButton.unhide();
            } else if (playState == "paused") {
                isPlaying = false;
                pauseButton.hide();
                playButton.unhide();
            }

            // Extract volume
            size_t volPos = pendingMessage.find("volume:");
            if (volPos != std::string::npos) {
                int newVol = std::atoi(pendingMessage.c_str() + volPos + 7);
                if (newVol >= 0 && newVol <= 100) {
                    setpoint = newVol;
                    lastSentSetpoint = setpoint;
                    volumeArc.setPercentage(setpoint);
                    ESP_LOGI(TAG, "Initial volume: %d", setpoint);
                }
            }

            if (volumeArc.isSegmentAnimationActive()) {
                volumeArc.stopSegmentAnimation();
            }

            stateReceived = true;
        }
    }
    // Handle play/pause
    else if (pendingMessage == "unpaused" || pendingMessage == "playing") {
        isPlaying = true;
        playButton.hide();
        pauseButton.unhide();
        if (volumeArc.isSegmentAnimationActive()) volumeArc.stopSegmentAnimation();
        if (!stateReceived) stateReceived = true;
    }
    else if (pendingMessage == "paused") {
        isPlaying = false;
        pauseButton.hide();
        playButton.unhide();
        if (volumeArc.isSegmentAnimationActive()) volumeArc.stopSegmentAnimation();
        if (!stateReceived) stateReceived = true;
    }
}

void SoundController::resetController() {
    pageBackRequested = true;
    volumeArc.reset();
    initialized = false;
    mqttInitialized = false;
    stateReceived = false;
    controllerState = CONTROLLER_INITIALIZE;
    ESP_LOGI(TAG, "Controller reset");
}

void SoundController::handleMediaButtonEvents() {
    bool mqttConnected = mqttConnectedCallback ? mqttConnectedCallback() : false;

    if (playButton.getIsVisible() && playButton.getHasNewRelease()) {
        if (mqttConnected && mqttPublishCallback) {
            mqttPublishCallback("esp32/sound/control", "play");
            ESP_LOGI(TAG, "Sent: play");
        }
    }

    if (pauseButton.getIsVisible() && pauseButton.getHasNewRelease()) {
        if (mqttConnected && mqttPublishCallback) {
            mqttPublishCallback("esp32/sound/control", "pause");
            ESP_LOGI(TAG, "Sent: pause");
        }
    }

    if (rewindButton.getHasNewRelease()) {
        if (mqttConnected && mqttPublishCallback) {
            mqttPublishCallback("esp32/sound/control", "rewind");
            ESP_LOGI(TAG, "Sent: rewind");
        }
    }

    if (forwardButton.getHasNewRelease()) {
        if (mqttConnected && mqttPublishCallback) {
            mqttPublishCallback("esp32/sound/control", "forward");
            ESP_LOGI(TAG, "Sent: forward");
        }
    }
}

void SoundController::drawMediaButtons() {
    if (playButton.getIsVisible()) playButton.draw();
    if (pauseButton.getIsVisible()) pauseButton.draw();
    rewindButton.draw();
    forwardButton.draw();
    backButton.draw();
}

void SoundController::updateMediaButtons() {
    playButton.update();
    pauseButton.update();
    rewindButton.update();
    forwardButton.update();
    backButton.update();

    static bool lastPlayPressed = false;
    static bool lastPausePressed = false;
    static bool lastRewindPressed = false;
    static bool lastForwardPressed = false;
    static bool lastBackPressed = false;

    if (playButton.getIsVisible() && playButton.getIsPressed() != lastPlayPressed) {
        playButton.draw();
        lastPlayPressed = playButton.getIsPressed();
    }
    if (pauseButton.getIsVisible() && pauseButton.getIsPressed() != lastPausePressed) {
        pauseButton.draw();
        lastPausePressed = pauseButton.getIsPressed();
    }
    if (rewindButton.getIsPressed() != lastRewindPressed) {
        rewindButton.draw();
        lastRewindPressed = rewindButton.getIsPressed();
    }
    if (forwardButton.getIsPressed() != lastForwardPressed) {
        forwardButton.draw();
        lastForwardPressed = forwardButton.getIsPressed();
    }
    if (backButton.getIsPressed() != lastBackPressed) {
        backButton.draw();
        lastBackPressed = backButton.getIsPressed();
    }
}

void SoundController::checkTouchInput() {
    if (touchPanel && touchPanel->isPressed()) {
        // Touch handling can be expanded based on UI layout
    }
}

void SoundController::drawStaticVolumeElements() {
    LvglDisplay::fillScreen(0x000000);
}

void SoundController::incrementSetpoint() {
    if (setpoint < 100) {
        setpoint++;
        lastSetpointChangeTime = millis();
        externalSetpointChange = false;
        volumeArc.setPercentage(setpoint);
    }
}

void SoundController::decrementSetpoint() {
    if (setpoint > 0) {
        setpoint--;
        lastSetpointChangeTime = millis();
        externalSetpointChange = false;
        volumeArc.setPercentage(setpoint);
    }
}

void SoundController::sendSetpointToServer() {
    if (mqttConnectedCallback && mqttConnectedCallback() && mqttPublishCallback) {
        std::string msg = "setpoint:" + std::to_string(setpoint);
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
        volumeArc.setPercentage(setpoint);
    }
}

void SoundController::resetPageBackRequest() {
    pageBackRequested = false;
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
    // No-op, wait for MQTT confirmation
}
