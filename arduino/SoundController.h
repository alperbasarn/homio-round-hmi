#ifndef SOUND_CONTROLLER_H
#define SOUND_CONTROLLER_H

#include <Arduino_GFX_Library.h>
#include "TouchPanel.h"
#include "MQTTHandler.h"
#include "Buttons.h"
#include "Arc.h"

// State machine enum
enum ControllerState {
  INITIALIZE,
  WAIT_FOR_SYNC,
  UPDATE
};

class SoundController {
private:
  Arduino_GFX* gfx;               // Graphics library instance
  TouchPanel* touchPanel;         // Touch panel handler
  MQTTHandler* mqttHandler;       // MQTT communication handler
  
  // Volume and state tracking
  int setpoint;                   // Current volume level (0-100)
  int lastSetpoint;               // Last displayed volume level
  int lastDrawnSetpoint;          // Last drawn volume in visualization
  int lastSentSetpoint;           // Last setpoint sent to server
  bool externalSetpointChange;    // Flag to track if setpoint change came from external source
  bool isPlaying;                 // Flag to track play/pause state
  
  // UI state tracking
  bool colorChange;               // Indicates if the color has changed
  bool initialized;               // Indicates if static elements have been drawn and state received
  bool mqttInitialized;           // Flag to track if MQTT is initialized and state request sent
  bool stateReceived;             // Flag to track if state has been received from server
  bool pageBackRequested;         // Flag for page-back request
  
  // State machine
  ControllerState controllerState; // Current state of the controller
  
  // Time management 
  unsigned long lastSentTime;     // General time tracker for periodic actions
  unsigned long lastSetpointChangeTime;  // Time when the setpoint was last changed
  unsigned long lastSetpointSentTime;    // Time when the setpoint was last sent
  
  // UI elements
  const int barCount = 20;        // Number of volume bars to display
  PlayButton playButton;          // Play button UI element
  PauseButton pauseButton;        // Pause button UI element
  RewindButton rewindButton;      // Rewind button UI element
  ForwardButton forwardButton;    // Forward button UI element
  BackButton backButton;          // Back button UI element
  Arc volumeArc;                  // Volume arc UI element

  // State machine methods
  void initializeController();    // Initialize UI and elements (INITIALIZE state)
  void waitForSync();             // Wait for MQTT sync (WAIT_FOR_SYNC state)
  void updateController();        // Regular operation mode (UPDATE state)
  
  // Internal methods for UI and control
  void drawStaticVolumeElements();          // Draw static sound visualization
  void drawSetPoint();                      // Draw the current volume level
  void checkTouchInput();                   // Check for touch input
  void sendSetpointToServer();              // Send volume setpoint to server via MQTT
  void requestInitialStateFromServer();     // Request initial state from server
  void drawMediaButtons();                  // Draw all media control buttons
  void updateMediaButtons();                // Update media button states
  void handleMediaButtonEvents();           // Handle media button events
  void processIncomingMQTTMessages();       // Process incoming MQTT messages
  void resetController();                   // Reset controller state and UI

public:
  // Constructor and destructor
  SoundController(Arduino_GFX* graphics, TouchPanel* touch, MQTTHandler* mqttHandler);
  
  // Main update method - call this in loop()
  void updateScreen();
  
  // Volume control methods
  void updateSetpoint(int setpoint);
  void incrementSetpoint();
  void decrementSetpoint();
  
  // Media control methods
  void togglePlayPause();         // Toggle between play and pause states
  
  // Navigation methods
  bool isPageBackRequested() const;
  void resetPageBackRequest();
  
  // Configuration methods
  void setMQTTHandler(MQTTHandler* mqttHandler);
};

#endif  // SOUND_CONTROLLER_H