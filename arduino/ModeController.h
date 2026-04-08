#ifndef MODE_CONTROLLER_H
#define MODE_CONTROLLER_H

#include <Arduino_GFX_Library.h>
#include "TouchPanel.h"
#include "WiFiTCPClient.h"

class ModeController {
private:
  Arduino_GFX* gfx;
  TouchPanel* touchPanel;
  WiFiTCPClient* tcpClient;
  int currentMode;
  int lastMode;  // Tracks the last drawn mode
  bool active;   // Determines if the controller should handle input
  const int modeCount = 3;

  void drawThermometer();
  void drawBulb();
  void drawSpeaker();
  void drawModeLogo();

public:
  ModeController(Arduino_GFX* graphics, TouchPanel* touch, WiFiTCPClient* client);

  void updateScreen();
  void nextMode();
  void previousMode();
  void setActive(bool isActive);  // Enable or disable the controller
  void setTCPClient(WiFiTCPClient* client); // Update TCP client reference
  int getCurrentMode() const;
  bool isSoundModeActive() const;
  bool isLightModeActive() const;
};

#endif  // MODE_CONTROLLER_H