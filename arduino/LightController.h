#ifndef LIGHT_CONTROLLER_H
#define LIGHT_CONTROLLER_H

#include <Arduino_GFX_Library.h>
#include "TouchPanel.h"
#include "MQTTHandler.h"

class LightController {
private:
  // Member variables
  Arduino_GFX* gfx;          // Pointer to the graphics object
  TouchPanel* touchPanel;    // Pointer to the touch screen object
  MQTTHandler* mqttHandler;  // Pointer to the MQTT handler
  int setpoint;                          // Current light setpoint (0-100)
  int lastSetpoint;                      // Last drawn setpoint
  int lastSentSetpoint;                  // Last setpoint sent to the server
  uint16_t lastSentColor;                // Last sent color as RGB565
  unsigned long lastSetpointChangeTime;  // Time when the setpoint was last changed
  unsigned long lastColorChangeTime;     // Time when the color was last changed
  unsigned long lastWaterLevelSentTime;  // Time when the water level was last sent to the server
  unsigned long lastSetPointDrawnTime;   // Time when the setpoint was last drawn
  unsigned long lastScreenUpdateTime;    // Time when the screen was last updated
  int lastSetpointQueryTime = 0;
  int waterLevel;          // Current water level (0-100)
  int lastWaterLevel;      // Last water level (0-100)
  float colorHue;          // Current hue for the light color
  float lastColorHue;      // Last hue for the light color
  bool colorChange;        // Indicates if the color has changed
  bool initialized;        // Indicates if static elements have been drawn
  bool pageBackRequested;  // Flag for page-back request
  int firstTouchX;         // Initial touch X-coordinate for swipe detection
  int firstTouchY;         // Initial touch Y-coordinate for swipe detection
  bool waterLevelChange;   // Indicates if the water level has changed
  enum LedMode { STATIC,
                 GLOW,
                 RAINBOW };  // LED modes
  LedMode currentMode;       // Current LED mode
  bool modeChangeRequested;  // Indicates if mode change is requested

  // Private methods
  void drawStaticElements();                                       // Draw static light visualization
  void drawSetPoint();                                             // Draw the current light setpoint and water level
  void drawWaterLevel(int level);                                  // Draw only the changed part of the water level
  void drawModeText();                                             // Draw the current LED mode text
  void sendSetpointToServer();                                     // Send the setpoint and color to the server
  void requestCurrentBrightnessFromServer();                       // Request the current brightness from the server
  uint16_t hsvToRgb565(float hue, float saturation, float value);  // Convert HSV to RGB565
  void checkTouchInput();                                          // Check for touch input and handle interactions
  void updateColorSpectrum();                                      // Update the color to shift through the spectrum
  void handleModeChange();                                         // Handle mode change logic

public:
  // Constructor
  LightController(Arduino_GFX* graphics, TouchPanel* touch, MQTTHandler* mqttHandler);

  // Public methods
  void updateScreen();               // Update the screen with the current state
  void setSetpoint(int setpoint);    // Increment the water level
  void incrementSetpoint();          // Increment the water level
  void decrementSetpoint();          // Decrement the water level
  bool isPageBackRequested() const;  // Check if a page-back request has been made
  void resetPageBackRequest();       // Reset the page-back request flag
  void setMQTTHandler(MQTTHandler* handler); // Update MQTT handler
};

#endif  // LIGHT_CONTROLLER_H