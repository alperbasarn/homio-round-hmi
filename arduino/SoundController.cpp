#include "SoundController.h"

SoundController::SoundController(Arduino_GFX* graphics, TouchPanel* touch, MQTTHandler* mqttHandler)
  : gfx(graphics), touchPanel(touch), mqttHandler(mqttHandler), setpoint(50), lastSetpoint(-1),
    lastSentSetpoint(-1), lastSetpointChangeTime(0), lastSetpointSentTime(0),
    initialized(false), mqttInitialized(false), stateReceived(false), pageBackRequested(false), lastDrawnSetpoint(-1),
    colorChange(false), lastSentTime(0), isPlaying(false), externalSetpointChange(false),
    controllerState(INITIALIZE),
    playButton(graphics, touch), pauseButton(graphics, touch),
    rewindButton(graphics, touch), forwardButton(graphics, touch), backButton(graphics, touch),
    volumeArc(graphics) {
  // Constructor only initializes variables
}

void SoundController::updateScreen() {
  // State machine for controller updates
  switch (controllerState) {
    case INITIALIZE:
      initializeController();
      // Move to next state
      controllerState = WAIT_FOR_SYNC;
      break;
    case WAIT_FOR_SYNC:
      waitForSync();
      // If we've received state from server, move to UPDATE state
      if (stateReceived) {
        Serial.println("State received, moving to UPDATE state");
        controllerState = UPDATE;
        volumeArc.stopSegmentAnimation();
      }
      break;
    case UPDATE:
      updateController();
      break;
  }

  // Always update the volume arc animation (needed in all states)
  volumeArc.update();

  // Always process touch input for back button in all states
  if (backButton.getHasNewRelease()) {
    resetController();
  }
}

void SoundController::initializeController() {
  // Clear the screen and initialize text properties
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(2);

  Serial.println("Drawing UI elements in INITIALIZE state");

  // Initialize media buttons
  int buttonSize = 60;
  int centerY = 120;
  int spacing = 60;

  // Initialize buttons with the proper positions
  playButton.initialize(120, centerY, buttonSize);
  pauseButton.initialize(120, centerY, buttonSize);
  rewindButton.initialize(120 - spacing, centerY, buttonSize);
  forwardButton.initialize(120 + spacing, centerY, buttonSize);

  // Initialize back button
  backButton.initialize(120, 210, 40, 20);

  // Initialize volume arc
  volumeArc.initialize(120, 120, 110, 15, 180, 180);
  volumeArc.setColor(0x07FF);  // Set a cyan color for the arc
  
  // Hide both buttons initially until we get status from server
  playButton.hide();
  pauseButton.hide();
  
  // Force draw all buttons (only visible ones will appear)
  forwardButton.draw();
  rewindButton.draw();
  backButton.draw();
  
  // Explicitly draw correct play/pause button
  if (isPlaying) {
    pauseButton.unhide();
    pauseButton.draw();
  } else {
    playButton.unhide();
    playButton.draw();
  }
  
  // Start segment animation until MQTT connection is established
  volumeArc.startSegmentAnimation();
  
  Serial.println("Controller initialized");
  
  initialized = true;
}

void SoundController::waitForSync() {
  // Check if MQTT is connected but we haven't sent initial state request yet
  if (mqttHandler && mqttHandler->isMQTTConnected() && !mqttInitialized) {
    // MQTT is now connected, safe to request initial state
    requestInitialStateFromServer();
    mqttInitialized = true;
    Serial.println("MQTT connected, sent initial state request");
  }
  
  // Update media buttons while waiting
  updateMediaButtons();

  // Process incoming MQTT messages (which may include state responses)
  processIncomingMQTTMessages();
  
  // If we've received state from server, move to UPDATE state
  if (stateReceived) {
    Serial.println("State received, moving to UPDATE state");
    controllerState = UPDATE;
  }
}

void SoundController::updateController() {
  // Update volume arc if setpoint changed
  if (setpoint != lastSetpoint) {
    volumeArc.setPercentage(setpoint);
    lastSetpoint = setpoint;
  }

  // Update media buttons
  updateMediaButtons();
  handleMediaButtonEvents();

  // Check for and process incoming MQTT messages
  processIncomingMQTTMessages();

  // Check for touch input
  checkTouchInput();

  // Check if the setpoint needs to be sent to the server
  // Only send if:
  // 1. The setpoint has changed from the last sent value
  // 2. The change was initiated locally (not from PC)
  // 3. Enough time has passed since the last send (debounce)
  unsigned long currentMillis = millis();
  if ((setpoint != lastSentSetpoint) && 
      !externalSetpointChange && 
      (currentMillis - lastSetpointSentTime >= 25)) {
    sendSetpointToServer();
    lastSetpointSentTime = currentMillis;  // Record the time of the last sent setpoint
    lastSentSetpoint = setpoint;           // Update the last sent setpoint
    
    Serial.print("Sent locally changed setpoint: ");
    Serial.println(setpoint);
  }

  // Reset the external change flag after processing
  if (externalSetpointChange) {
    externalSetpointChange = false;
  }
}

void SoundController::processIncomingMQTTMessages() {
  if (mqttHandler && mqttHandler->getHasSoundMessage()) {
    String message = mqttHandler->getSoundMessage();
    Serial.print("Processing sound message: ");
    Serial.println(message);

    // Note: MQTTHandler has already filtered out messages from self
    // and extracted the actual command part from the message

    // Check if the message is a setpoint command
    if (message.startsWith("setpoint:")) {
      String valueStr = message.substring(9);
      int newSetpoint = valueStr.toInt();

      // Validate setpoint range
      if (newSetpoint >= 0 && newSetpoint <= 100) {
        // Set the flag to indicate this change came from external source
        externalSetpointChange = true;
        
        // Only update if the setpoint is different
        if (newSetpoint != setpoint) {
          setpoint = newSetpoint;
          lastSetpointChangeTime = millis();

          // Update the volume arc with the new setpoint
          volumeArc.setPercentage(setpoint);
          
          // Also update lastSentSetpoint to avoid sending it back
          lastSentSetpoint = setpoint;

          Serial.print("Setpoint updated from MQTT: ");
          Serial.println(setpoint);
          
          // If the arc was in animation mode, stop it since we have real data now
          if (volumeArc.isSegmentAnimationActive()) {
            volumeArc.stopSegmentAnimation();
          }
        } else {
          Serial.println("Received same setpoint value, ignoring");
        }
      } else {
        Serial.println("Invalid setpoint value received");
      }
    }
    // Handle state response message
    else if (message.startsWith("state:")) {
      // Format should be "state:playing,volume:50" or "state:paused,volume:75"
      String stateStr = message.substring(6); // Skip "state:"
      
      // Extract state part (playing/paused)
      int commaPos = stateStr.indexOf(',');
      if (commaPos > 0) {
        String playState = stateStr.substring(0, commaPos);
        
        // Update playing state
        if (playState == "playing") {
          isPlaying = true;
          playButton.hide();
          pauseButton.unhide();
          Serial.println("Initial state: playing");
        } else if (playState == "paused") {
          isPlaying = false;
          pauseButton.hide();
          playButton.unhide();
          Serial.println("Initial state: paused");
        }
        
        // Extract volume part if present
        if (stateStr.indexOf("volume:") >= 0) {
          int volPos = stateStr.indexOf("volume:") + 7; // Skip "volume:"
          String volStr = stateStr.substring(volPos);
          int newVol = volStr.toInt();
          
          if (newVol >= 0 && newVol <= 100) {
            setpoint = newVol;
            lastSentSetpoint = setpoint;
            volumeArc.setPercentage(setpoint);
            Serial.print("Initial volume set to: ");
            Serial.println(setpoint);
          }
        }
        
        // Stop the arc animation since we now have real data
        if (volumeArc.isSegmentAnimationActive()) {
          volumeArc.stopSegmentAnimation();
        }
        
        // Mark state as received
        stateReceived = true;
        Serial.println("State received from server");
      }
    }
    // Handle play/pause state commands
    else if (message == "unpaused" || message == "playing") {
      // Update play/pause button state for playing state
      isPlaying = true;
      playButton.hide();
      pauseButton.unhide();
      Serial.println("Media state changed to playing via MQTT");
      
      // Stop animation if it was still running
      if (volumeArc.isSegmentAnimationActive()) {
        volumeArc.stopSegmentAnimation();
      }
      
      // Mark state as received if not already
      if (!stateReceived) {
        stateReceived = true;
        Serial.println("State received (playing)");
      }
    } 
    else if (message == "paused") {
      // Update play/pause button state for paused state
      isPlaying = false;
      pauseButton.hide();
      playButton.unhide();
      Serial.println("Media state changed to paused via MQTT");
      
      // Stop animation if it was still running
      if (volumeArc.isSegmentAnimationActive()) {
        volumeArc.stopSegmentAnimation();
      }
      
      // Mark state as received if not already
      if (!stateReceived) {
        stateReceived = true;
        Serial.println("State received (paused)");
      }
    }
    else if (message == "forward") {
      // Handle forward command
      Serial.println("Received forward command");
      // Implement any UI/feedback for forward command here
    }
    else if (message == "rewind") {
      // Handle rewind command
      Serial.println("Received rewind command");
      // Implement any UI/feedback for rewind command here
    }
    // Add other message types handling as needed
  }
}

void SoundController::resetController() {
  // Reset the controller state
  pageBackRequested = true;
  
  // Reset the volume arc
  volumeArc.reset();
  
  // Reset state tracking variables
  initialized = false;
  mqttInitialized = false;
  stateReceived = false;
  
  // Immediately set state machine back to INITIALIZE
  controllerState = INITIALIZE;
  
  Serial.println("Controller reset, returning to INITIALIZE state");
}

void SoundController::handleMediaButtonEvents() {
  // Check if play button was released
  if (playButton.getIsVisible() && playButton.getHasNewRelease()) {
    // Only send MQTT message for play if MQTT is connected
    if (mqttHandler && mqttHandler->isMQTTConnected()) {
      mqttHandler->sendMQTTMessage("esp32/sound/control", "play");
      Serial.println("Sound control sent via MQTT: play");
      // We don't set isPlaying here anymore - wait for confirmation via MQTT
    }
  }

  // Check if pause button was released
  if (pauseButton.getIsVisible() && pauseButton.getHasNewRelease()) {
    // Only send MQTT message for pause if MQTT is connected
    if (mqttHandler && mqttHandler->isMQTTConnected()) {
      mqttHandler->sendMQTTMessage("esp32/sound/control", "pause");
      Serial.println("Sound control sent via MQTT: pause");
      // We don't set isPlaying here anymore - wait for confirmation via MQTT
    }
  }

  // Check if rewind button was released
  if (rewindButton.getHasNewRelease()) {
    // Send MQTT message for rewind only if MQTT is connected
    if (mqttHandler && mqttHandler->isMQTTConnected()) {
      mqttHandler->sendMQTTMessage("esp32/sound/control", "rewind");
      Serial.println("Sound control sent via MQTT: rewind");
    }
  }

  // Check if forward button was released
  if (forwardButton.getHasNewRelease()) {
    // Send MQTT message for forward only if MQTT is connected
    if (mqttHandler && mqttHandler->isMQTTConnected()) {
      mqttHandler->sendMQTTMessage("esp32/sound/control", "forward");
      Serial.println("Sound control sent via MQTT: forward");
    }
  }
}

// Other methods remain the same as before...
// Including: drawMediaButtons, updateMediaButtons, checkTouchInput, etc.

void SoundController::drawMediaButtons() {
  // Only draw visible buttons
  if (playButton.getIsVisible()) {
    playButton.draw();
  }
  if (pauseButton.getIsVisible()) {
    pauseButton.draw();
  }
  rewindButton.draw();
  forwardButton.draw();
  backButton.draw();
}

void SoundController::updateMediaButtons() {
  // Update all buttons
  playButton.update();
  pauseButton.update();
  rewindButton.update();
  forwardButton.update();
  backButton.update();

  // Draw buttons if their state has changed
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
  // Check if touch panel is available
  if (touchPanel) {
    // Process touch input if available
    if (touchPanel->isPressed()) {
      // Get the touch coordinates
      int touchX = touchPanel->getTouchX();
      int touchY = touchPanel->getTouchY();

      // Process touch input based on location
      // This could be expanded based on your UI layout
    }
  }
}

void SoundController::drawStaticVolumeElements() {
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(2);
}

void SoundController::incrementSetpoint() {
  if (setpoint < 100) {
    setpoint++;
    lastSetpointChangeTime = millis();  // Update the last change timestamp
    
    // Local change, not from external source
    externalSetpointChange = false;

    // Update the volume arc
    volumeArc.setPercentage(setpoint);
  }
}

void SoundController::decrementSetpoint() {
  if (setpoint > 0) {
    setpoint--;
    lastSetpointChangeTime = millis();  // Update the last change timestamp
    
    // Local change, not from external source
    externalSetpointChange = false;

    // Update the volume arc
    volumeArc.setPercentage(setpoint);
  }
}

void SoundController::sendSetpointToServer() {
  // Only send setpoint via MQTT if MQTT is connected
  if (mqttHandler && mqttHandler->isMQTTConnected()) {
    mqttHandler->sendMQTTMessage("esp32/sound/setpoint", "setpoint:" + String(setpoint));
    Serial.println("Sound setpoint sent via MQTT: " + String(setpoint));
  } else {
    Serial.println("MQTT not connected, skipping setpoint send");
  }
}

void SoundController::requestInitialStateFromServer() {
  // Request initial state and setpoint via MQTT if MQTT is connected
  if (mqttHandler && mqttHandler->isMQTTConnected()) {
    mqttHandler->sendMQTTMessage("esp32/sound/get_state", "request");
    Serial.println("Sent get_state request via MQTT.");
  } else {
    Serial.println("MQTT not connected, cannot send state request");
  }
}

void SoundController::updateSetpoint(int level) {
  // Only update if the value has changed
  if (level != setpoint) {
    setpoint = level;
    lastSetpointChangeTime = millis();  // Update the last change timestamp
    
    // Mark as local change, not external
    externalSetpointChange = false;

    // Update the volume arc
    volumeArc.setPercentage(setpoint);
  }
}

bool SoundController::isPageBackRequested() const {
  return pageBackRequested;
}

void SoundController::resetPageBackRequest() {
  pageBackRequested = false;
  
  // Make sure we redraw the UI when returning to this screen
  controllerState = INITIALIZE;
  initialized = false;
  mqttInitialized = false;
  stateReceived = false;
  
  Serial.println("Page back request reset, forcing redraw on next update");
}

void SoundController::setMQTTHandler(MQTTHandler* handler) {
  mqttHandler = handler;
}

void SoundController::togglePlayPause() {
  // This method is no longer used since we wait for MQTT confirmation
  // but kept for API compatibility
}