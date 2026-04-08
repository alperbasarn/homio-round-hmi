#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <Arduino.h>
#include "DisplayController.h"
#include "EEPROMManager.h"
#include <WiFi.h>

// Forward declaration
class KnobController;
class WiFiTCPClient;

class CommandHandler {
private:
  bool hasNewMessage;
  typedef void (CommandHandler::*CommandFunction)(const String& params);

  struct Command {
    String name;
    CommandFunction callback;
    String description;
  };

  // Command storage
  Command commands[30];  // Increased to support more commands
  int commandCount = 0;

  // Dependencies
  DisplayController* displayController;
  EEPROMManager* eepromManager;
  KnobController* knobController;  // Pointer to KnobController
  WiFiTCPClient* tcpClient;        // Pointer to TCP client for handling TCP commands

  // Command buffer
  String commandFromPC;

  // Command handlers - existing commands
  void cmdIncrementSetpoint(const String& params);
  void cmdDecrementSetpoint(const String& params);
  void cmdReset(const String& params);
  void cmdSwitchToHome(const String& params);
  void cmdSwitchToInfo(const String& params);
  void cmdClearFlash(const String& params);
  void cmdConnectWifi(const String& params);
  void cmdClearEepromSlot(const String& params);
  void cmdShowNetworks(const String& params);
  void cmdCalibrateOrientation(const String& params);
  void cmdHelp(const String& params);
  void cmdKnobSetpoint(const String& params);

  // EEPROM Get/Set value handlers
  void cmdGetEEPROMValue(const String& params);
  void cmdSetEEPROMValue(const String& params);
  void cmdListEEPROMValues(const String& params);

  // Server configuration command handlers
  void cmdConfigureSoundTCPServer(const String& params);
  void cmdConfigureLightTCPServer(const String& params);
  void cmdConfigureSoundMQTTServer(const String& params);
  void cmdConfigureLightMQTTServer(const String& params);
  void cmdCommInfo(const String& params);
  void cmdSetDeviceName(const String& params);

  // Static IP configuration command handlers
  void cmdConfigureStaticIP(const String& params);
  void cmdEnableStaticIP(const String& params);
  void cmdDisableStaticIP(const String& params);
  void cmdShowStaticIP(const String& params);

  // Process command from various sources
  void processKnobCommand(const String& command);
  void processTCPCommand(const String& command);

  // Helper method for TCP responses
  void sendTCPResponse(const String& response);
  void cmdGetDeviceName(const String& params);
public:
  CommandHandler(DisplayController* dc, EEPROMManager* em, WiFiTCPClient* tcp);
  void initialize();
  void update();
  void printHelp();
  bool getHasNewMessage();
  // Method to register KnobController
  void registerKnobController(KnobController* kc);
};

#endif  // COMMAND_HANDLER_H