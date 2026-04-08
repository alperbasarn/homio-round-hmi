#include "CommandHandler.h"
#include "KnobController.h"  // Include KnobController definition
#include "WiFiTCPClient.h"   // Include WiFiTCPClient definition

CommandHandler::CommandHandler(DisplayController* dc, EEPROMManager* em, WiFiTCPClient* tcp)
  : displayController(dc), eepromManager(em), knobController(nullptr), tcpClient(tcp), commandCount(0) {
}

void CommandHandler::initialize() {
  Serial.begin(115200);

  // Register existing commands
  commands[commandCount++] = { "+", &CommandHandler::cmdIncrementSetpoint, "Increment setpoint" };
  commands[commandCount++] = { "-", &CommandHandler::cmdDecrementSetpoint, "Decrement setpoint" };
  commands[commandCount++] = { "reset", &CommandHandler::cmdReset, "Restart ESP" };
  commands[commandCount++] = { "home", &CommandHandler::cmdSwitchToHome, "Switch to home page" };
  commands[commandCount++] = { "info", &CommandHandler::cmdSwitchToInfo, "Switch to info page" };
  commands[commandCount++] = { "clearFlash", &CommandHandler::cmdClearFlash, "Remove all saved data from flash" };
  commands[commandCount++] = { "connectWifi", &CommandHandler::cmdConnectWifi, "connectWifi:SSID:Password:Slot - Save and connect to Wi-Fi (slot 0-2)" };
  commands[commandCount++] = { "clearEepromSlot", &CommandHandler::cmdClearEepromSlot, "clearEepromSlot:slotNumber - Clear a specific EEPROM slot (0-2)" };
  commands[commandCount++] = { "showNetworks", &CommandHandler::cmdShowNetworks, "Show stored Wi-Fi networks" };
  commands[commandCount++] = { "calibrateOrientation", &CommandHandler::cmdCalibrateOrientation, "Calibrate device orientation" };
  commands[commandCount++] = { "setpoint", &CommandHandler::cmdKnobSetpoint, "Set the setpoint value" };
  commands[commandCount++] = { "getDeviceName", &CommandHandler::cmdGetDeviceName, "Get the device name" };
  // Register EEPROM configuration commands
  commands[commandCount++] = { "getEEPROMValue", &CommandHandler::cmdGetEEPROMValue, "getEEPROMValue:key - Get value from EEPROM by key" };
  commands[commandCount++] = { "setEEPROMValue", &CommandHandler::cmdSetEEPROMValue, "setEEPROMValue:key:value - Set value in EEPROM by key" };
  commands[commandCount++] = { "listEEPROMValues", &CommandHandler::cmdListEEPROMValues, "List all EEPROM values" };

  // Register existing server configuration commands
  commands[commandCount++] = { "configureSoundTCPServer", &CommandHandler::cmdConfigureSoundTCPServer, "configureSoundTCPServer:IP:Port - Configure Sound TCP Server" };
  commands[commandCount++] = { "configureLightTCPServer", &CommandHandler::cmdConfigureLightTCPServer, "configureLightTCPServer:IP:Port - Configure Light TCP Server" };
  commands[commandCount++] = { "configureSoundMQTTServer", &CommandHandler::cmdConfigureSoundMQTTServer, "configureSoundMQTTServer:URL:Port:Username:Password - Configure Sound MQTT Server" };
  commands[commandCount++] = { "configureLightMQTTServer", &CommandHandler::cmdConfigureLightMQTTServer, "configureLightMQTTServer:URL:Port:Username:Password - Configure Light MQTT Server" };
  commands[commandCount++] = { "commInfo", &CommandHandler::cmdCommInfo, "Display all communication configuration information" };
  commands[commandCount++] = { "setDeviceName", &CommandHandler::cmdSetDeviceName, "setDeviceName:name - Set device name" };

  // Register static IP configuration commands
  commands[commandCount++] = { "configureStaticIP", &CommandHandler::cmdConfigureStaticIP, "configureStaticIP:IP:Gateway:Subnet:DNS1:DNS2 - Configure static IP settings" };
  commands[commandCount++] = { "enableStaticIP", &CommandHandler::cmdEnableStaticIP, "Enable static IP configuration" };
  commands[commandCount++] = { "disableStaticIP", &CommandHandler::cmdDisableStaticIP, "Disable static IP configuration (use DHCP)" };
  commands[commandCount++] = { "showStaticIP", &CommandHandler::cmdShowStaticIP, "Show current static IP configuration" };

  commands[commandCount++] = { "help", &CommandHandler::cmdHelp, "Show available commands" };

  Serial.println("Serial interface initialized.");
}

void CommandHandler::registerKnobController(KnobController* kc) {
  knobController = kc;
}

// Replace the update() method in CommandHandler.cpp with this optimized version
void CommandHandler::update() {
  hasNewMessage = false;

  // Process one source per cycle rather than all at once
  static uint8_t sourceIndex = 0;

  // Round-robin check different command sources
  switch (sourceIndex) {
    case 0:  // PC Serial
      if (Serial.available()) {
        commandFromPC = "";
        hasNewMessage = true;

        // Read only up to a newline or a maximum of bytes
        int bytesRead = 0;
        const int MAX_BYTES = 64;

        while (Serial.available() && bytesRead < MAX_BYTES) {
          char c = Serial.read();
          bytesRead++;

          if (c == '\n') break;  // End of command
          commandFromPC += c;
        }

        if (commandFromPC.length() > 0) {
          // Extract command name and parameters
          String cmdName;
          String params = "";

          int colonPos = commandFromPC.indexOf(':');
          if (colonPos != -1) {
            cmdName = commandFromPC.substring(0, colonPos);
            params = commandFromPC.substring(colonPos + 1);
          } else {
            cmdName = commandFromPC;
          }

          // Find and execute command
          bool found = false;
          for (int i = 0; i < commandCount; i++) {
            if (commands[i].name == cmdName) {
              found = true;
              (this->*commands[i].callback)(params);
              break;
            }
          }

          if (!found) {
            Serial.println("[❌ Error] Unknown command. Type 'help' for available commands.");
          }
        }
      }
      break;

    case 1:  // Knob Controller
      if (knobController && knobController->getHasNewMessage()) {
        hasNewMessage = true;
        String knobCommand = knobController->getReceivedCommand();
        processKnobCommand(knobCommand);
      }
      break;

    case 2:  // TCP Client
      if (tcpClient && tcpClient->getHasNewMessage()) {
        hasNewMessage = true;
        String tcpCommand = tcpClient->getMessage();
        processTCPCommand(tcpCommand);
      }
      break;
  }

  // Move to next source for next cycle
  sourceIndex = (sourceIndex + 1) % 3;
}

void CommandHandler::processKnobCommand(const String& command) {
  // Handle specific knob commands
  if (command == "+") {
    cmdIncrementSetpoint("");
  } else if (command == "-") {
    cmdDecrementSetpoint("");
  } else if (command == "reset") {
    cmdReset("");
  } else if (command.equals("calibrate")) {
    // Special handling for calibrate command
    displayController->setSetpoint(1);
    cmdCalibrateOrientation("");
    if (knobController) {
      knobController->sendCommand("switching to calibration");
    }
  } else if (command.startsWith("setpoint=")) {
    // Extract setpoint value and pass to handler
    cmdKnobSetpoint(command.substring(9));
  } else {
    Serial.println("Unknown command from knob: " + command);
  }
}

void CommandHandler::processTCPCommand(const String& command) {
  // Process commands received via TCP server
  // Extract command name and parameters
  String cmdName;
  String params = "";

  int colonPos = command.indexOf(':');
  if (colonPos != -1) {
    cmdName = command.substring(0, colonPos);
    params = command.substring(colonPos + 1);
  } else {
    cmdName = command;
  }

  // Find and execute command
  bool found = false;
  for (int i = 0; i < commandCount; i++) {
    if (commands[i].name == cmdName) {
      found = true;
      (this->*commands[i].callback)(params);

      // Send success response to TCP client
      sendTCPResponse("Command executed: " + cmdName);
      break;
    }
  }

  if (!found) {
    String errorMsg = "[Error] Unknown TCP command: " + command;
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    sendTCPResponse("Type 'help' for available commands");
  }
}

void CommandHandler::sendTCPResponse(const String& response) {
  if (tcpClient) {
    tcpClient->sendTCPResponse(response);
  }
}

void CommandHandler::printHelp() {
  String helpText = "\n🛠 Available Commands:";
  Serial.println(helpText);
  sendTCPResponse(helpText);

  for (int i = 0; i < commandCount; i++) {
    String cmdInfo = "  ";
    cmdInfo += commands[i].name;
    if (commands[i].name.indexOf(':') != -1) {
      cmdInfo += "[params]";
    }
    cmdInfo += " - ";
    cmdInfo += commands[i].description;

    Serial.println(cmdInfo);
    sendTCPResponse(cmdInfo);
  }
}

// Existing command implementations
void CommandHandler::cmdIncrementSetpoint(const String& params) {
  displayController->incrementSetpoint();
  Serial.println("SetPoint incremented.");
}

void CommandHandler::cmdDecrementSetpoint(const String& params) {
  displayController->decrementSetpoint();
  Serial.println("SetPoint decremented.");
}

void CommandHandler::cmdReset(const String& params) {
  Serial.println("🔄 Restarting ESP...");
  esp_restart();
}

void CommandHandler::cmdSwitchToHome(const String& params) {
  displayController->setMode(HOME);
  Serial.println("🏠 Switched to home page.");
}

void CommandHandler::cmdSwitchToInfo(const String& params) {
  displayController->setMode(INFO);
  Serial.println("🏠 Switched to home page.");
}

void CommandHandler::cmdClearFlash(const String& params) {
  eepromManager->clearEntireEEPROM();
  Serial.println("Flash memory cleared.");
}

void CommandHandler::cmdConnectWifi(const String& params) {
  // Parse Wi-Fi credentials
  int firstSep = params.indexOf(':');
  int secondSep = params.lastIndexOf(':');

  if (firstSep == -1 || secondSep == firstSep) {
    String errorMsg = "[WiFi] Invalid format! Use: connectWifi:SSID:Password:Slot";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    return;
  }

  String ssid = params.substring(0, firstSep);
  String password = params.substring(firstSep + 1, secondSep);
  int slot = params.substring(secondSep + 1).toInt();

  if (slot < 0 || slot > 2) {
    String warnMsg = "[WiFi] Invalid slot! Must be 0, 1, or 2. Defaulting to slot 1.";
    Serial.println(warnMsg);
    sendTCPResponse(warnMsg);
    slot = 1;
  }

  String infoMsg = "[WiFi] Saving to Slot " + String(slot) + " - SSID: " + ssid + ", Password: " + password;
  Serial.println(infoMsg);
  sendTCPResponse(infoMsg);

  // Save WiFi credentials using EEPROMManager
  eepromManager->wifiCredentials[slot].ssid = ssid;
  eepromManager->wifiCredentials[slot].password = password;
  eepromManager->wifiCredentials[slot].remember = true;
  eepromManager->lastConnectedNetworkIndex = slot;
  eepromManager->saveWiFiCredentials();  // Ensure the credentials persist in memory

  String saveMsg = "[WiFi] Credentials saved. Attempting connection...";
  Serial.println(saveMsg);
  sendTCPResponse(saveMsg);

  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), password.c_str());

  sendTCPResponse("[WiFi] Connecting...");
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    String successMsg = "\n✅ Successfully connected to WiFi! IP: " + WiFi.localIP().toString();
    Serial.println(successMsg);
    sendTCPResponse(successMsg);
  } else {
    String failMsg = "\n❌ WiFi Connection failed.";
    Serial.println(failMsg);
    sendTCPResponse(failMsg);
  }
}

void CommandHandler::cmdClearEepromSlot(const String& params) {
  int slot = params.toInt();
  if (slot < 0 || slot > 2) {
    Serial.println("[EEPROM] Invalid slot! Must be 0, 1, or 2.");
    return;
  }

  Serial.printf("[EEPROM] Clearing slot %d\n", slot);
  eepromManager->wifiCredentials[slot].ssid = "";
  eepromManager->wifiCredentials[slot].password = "";
  eepromManager->wifiCredentials[slot].remember = false;
  eepromManager->saveWiFiCredentials();  // Save cleared slot to EEPROM
  Serial.println("[EEPROM] Slot cleared successfully.");
}

void CommandHandler::cmdShowNetworks(const String& params) {
  Serial.println("\n📡 Stored Wi-Fi Networks:");
  for (int i = 0; i < 3; i++) {
    Serial.printf("[Slot %d] SSID: %s, Password: %s\n",
                  i, eepromManager->wifiCredentials[i].ssid.c_str(),
                  eepromManager->wifiCredentials[i].password.c_str());
  }
}

void CommandHandler::cmdCalibrateOrientation(const String& params) {
  displayController->setMode(CALIBRATE_ORIENTATION);
  Serial.println("Starting orientation calibration...");
}

void CommandHandler::cmdHelp(const String& params) {
  printHelp();
}

void CommandHandler::cmdKnobSetpoint(const String& params) {
  int setPoint = params.toInt();
  displayController->setSetpoint(setPoint);
  Serial.print("Updated setpoint: ");
  Serial.println(setPoint);
}

// New command implementations
void CommandHandler::cmdConfigureSoundTCPServer(const String& params) {
  // Parse IP and port
  int colonPos = params.indexOf(':');
  if (colonPos == -1) {
    String errorMsg = "[Error] Invalid format! Use: configureSoundTCPServer:IP:Port";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    return;
  }

  String ipAddress = params.substring(0, colonPos);
  int port = params.substring(colonPos + 1).toInt();

  if (port <= 0 || port > 65535) {
    String errorMsg = "[Error] Invalid port number. Must be between 1 and 65535.";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    return;
  }

  // Save configuration to EEPROM
  eepromManager->saveSoundTCPServer(ipAddress, port);
  String successMsg1 = "[Config] Sound TCP Server configured:";
  String successMsg2 = "  IP: " + ipAddress;
  String successMsg3 = "  Port: " + String(port);

  Serial.println(successMsg1);
  Serial.println(successMsg2);
  Serial.println(successMsg3);

  sendTCPResponse(successMsg1);
  sendTCPResponse(successMsg2);
  sendTCPResponse(successMsg3);
}

void CommandHandler::cmdConfigureLightTCPServer(const String& params) {
  // Parse IP and port
  int colonPos = params.indexOf(':');
  if (colonPos == -1) {
    String errorMsg = "[Error] Invalid format! Use: configureLightTCPServer:IP:Port";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    return;
  }

  String ipAddress = params.substring(0, colonPos);
  int port = params.substring(colonPos + 1).toInt();

  if (port <= 0 || port > 65535) {
    String errorMsg = "[Error] Invalid port number. Must be between 1 and 65535.";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    return;
  }

  // Save configuration to EEPROM
  eepromManager->saveLightTCPServer(ipAddress, port);
  String successMsg1 = "[Config] Light TCP Server configured:";
  String successMsg2 = "  IP: " + ipAddress;
  String successMsg3 = "  Port: " + String(port);

  Serial.println(successMsg1);
  Serial.println(successMsg2);
  Serial.println(successMsg3);

  sendTCPResponse(successMsg1);
  sendTCPResponse(successMsg2);
  sendTCPResponse(successMsg3);
}

void CommandHandler::cmdConfigureSoundMQTTServer(const String& params) {
  // Parse URL, port, username, and password
  int firstColon = params.indexOf(':');
  int secondColon = params.indexOf(':', firstColon + 1);
  int thirdColon = params.indexOf(':', secondColon + 1);

  if (firstColon == -1 || secondColon == -1 || thirdColon == -1) {
    String errorMsg = "[Error] Invalid format! Use: configureSoundMQTTServer:URL:Port:Username:Password";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    return;
  }

  String url = params.substring(0, firstColon);
  int port = params.substring(firstColon + 1, secondColon).toInt();
  String username = params.substring(secondColon + 1, thirdColon);
  String password = params.substring(thirdColon + 1);

  if (port <= 0 || port > 65535) {
    String errorMsg = "[Error] Invalid port number. Must be between 1 and 65535.";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    return;
  }

  // Save configuration to EEPROM
  eepromManager->saveSoundMQTTServer(url, port, username, password);
  String successMsg1 = "[Config] Sound MQTT Server configured:";
  String successMsg2 = "  URL: " + url;
  String successMsg3 = "  Port: " + String(port);
  String successMsg4 = "  Username: " + username;

  Serial.println(successMsg1);
  Serial.println(successMsg2);
  Serial.println(successMsg3);
  Serial.println(successMsg4);

  sendTCPResponse(successMsg1);
  sendTCPResponse(successMsg2);
  sendTCPResponse(successMsg3);
  sendTCPResponse(successMsg4);
}

void CommandHandler::cmdConfigureLightMQTTServer(const String& params) {
  // Parse URL, port, username, and password
  int firstColon = params.indexOf(':');
  int secondColon = params.indexOf(':', firstColon + 1);
  int thirdColon = params.indexOf(':', secondColon + 1);

  if (firstColon == -1 || secondColon == -1 || thirdColon == -1) {
    String errorMsg = "[Error] Invalid format! Use: configureLightMQTTServer:URL:Port:Username:Password";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    return;
  }

  String url = params.substring(0, firstColon);
  int port = params.substring(firstColon + 1, secondColon).toInt();
  String username = params.substring(secondColon + 1, thirdColon);
  String password = params.substring(thirdColon + 1);

  if (port <= 0 || port > 65535) {
    String errorMsg = "[Error] Invalid port number. Must be between 1 and 65535.";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    return;
  }

  // Save configuration to EEPROM
  eepromManager->saveLightMQTTServer(url, port, username, password);
  String successMsg1 = "[Config] Light MQTT Server configured:";
  String successMsg2 = "  URL: " + url;
  String successMsg3 = "  Port: " + String(port);
  String successMsg4 = "  Username: " + username;

  Serial.println(successMsg1);
  Serial.println(successMsg2);
  Serial.println(successMsg3);
  Serial.println(successMsg4);

  sendTCPResponse(successMsg1);
  sendTCPResponse(successMsg2);
  sendTCPResponse(successMsg3);
  sendTCPResponse(successMsg4);
}

void CommandHandler::cmdCommInfo(const String& params) {
  // Display all communication configuration information
  String info = eepromManager->getConfigurationInfo();
  Serial.println(info);

  // Split the info into lines for TCP response
  int startPos = 0;
  int endPos = info.indexOf('\n');
  while (endPos != -1) {
    String line = info.substring(startPos, endPos);
    sendTCPResponse(line);
    startPos = endPos + 1;
    endPos = info.indexOf('\n', startPos);
  }
  // Send last line if any
  if (startPos < info.length()) {
    sendTCPResponse(info.substring(startPos));
  }
}

void CommandHandler::cmdSetDeviceName(const String& params) {
  if (params.length() == 0) {
    String errorMsg = "[Error] Device name cannot be empty!";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    return;
  }

  eepromManager->saveDeviceName(params);
  String successMsg = "Device name set to: " + params;
  Serial.println(successMsg);
  sendTCPResponse(successMsg);
}

// New command implementations for static IP configuration
void CommandHandler::cmdConfigureStaticIP(const String& params) {
  // Parse the parameters
  int firstColon = params.indexOf(':');
  int secondColon = params.indexOf(':', firstColon + 1);
  int thirdColon = params.indexOf(':', secondColon + 1);
  int fourthColon = params.indexOf(':', thirdColon + 1);

  if (firstColon == -1 || secondColon == -1 || thirdColon == -1) {
    String errorMsg = "[Error] Invalid format! Use: configureStaticIP:IP:Gateway:Subnet:DNS1:DNS2";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    return;
  }

  String ip = params.substring(0, firstColon);
  String gateway = params.substring(firstColon + 1, secondColon);
  String subnet = params.substring(secondColon + 1, thirdColon);

  String dns1 = "8.8.8.8";  // Default primary DNS
  String dns2 = "8.8.4.4";  // Default secondary DNS

  if (thirdColon != -1) {
    if (fourthColon != -1) {
      dns1 = params.substring(thirdColon + 1, fourthColon);
      dns2 = params.substring(fourthColon + 1);
    } else {
      dns1 = params.substring(thirdColon + 1);
    }
  }

  // Validate IP addresses
  IPAddress ipAddr, gwAddr, subnetAddr, dns1Addr, dns2Addr;
  if (!ipAddr.fromString(ip) || !gwAddr.fromString(gateway) || !subnetAddr.fromString(subnet) || !dns1Addr.fromString(dns1) || !dns2Addr.fromString(dns2)) {
    String errorMsg = "[Error] Invalid IP address format!";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
    return;
  }

  // Save to EEPROM
  eepromManager->saveStaticIPConfig(true, ip, gateway, subnet, dns1, dns2);

  // Apply configuration to TCPHandler via WiFiTCPClient
  if (tcpClient) {
    // The TCP client will apply the configuration to TCPHandler
    String successMsg = "[Config] Static IP configuration saved and will be applied on next restart";
    Serial.println(successMsg);
    sendTCPResponse(successMsg);

    // Send configuration details
    sendTCPResponse("  IP: " + ip);
    sendTCPResponse("  Gateway: " + gateway);
    sendTCPResponse("  Subnet: " + subnet);
    sendTCPResponse("  DNS1: " + dns1);
    sendTCPResponse("  DNS2: " + dns2);
  } else {
    String errorMsg = "[Error] TCP client not available!";
    Serial.println(errorMsg);
    sendTCPResponse(errorMsg);
  }
}

void CommandHandler::cmdEnableStaticIP(const String& params) {
  // Enable static IP using current configuration
  eepromManager->staticIPEnabled = true;
  eepromManager->saveStaticIPConfig(
    true,
    eepromManager->staticIP,
    eepromManager->staticGateway,
    eepromManager->staticSubnet,
    eepromManager->staticDNS1,
    eepromManager->staticDNS2);

  String successMsg = "[Config] Static IP enabled. Configuration will be applied on next restart.";
  Serial.println(successMsg);
  sendTCPResponse(successMsg);
}

void CommandHandler::cmdDisableStaticIP(const String& params) {
  // Disable static IP (use DHCP)
  eepromManager->staticIPEnabled = false;
  eepromManager->saveStaticIPConfig(
    false,
    eepromManager->staticIP,
    eepromManager->staticGateway,
    eepromManager->staticSubnet,
    eepromManager->staticDNS1,
    eepromManager->staticDNS2);

  String successMsg = "[Config] Static IP disabled. DHCP will be used on next restart.";
  Serial.println(successMsg);
  sendTCPResponse(successMsg);
}

void CommandHandler::cmdShowStaticIP(const String& params) {
  // Display static IP configuration
  String info = eepromManager->getStaticIPInfo();
  Serial.println(info);

  // Split the info into lines for TCP response
  int startPos = 0;
  int endPos = info.indexOf('\n');
  while (endPos != -1) {
    String line = info.substring(startPos, endPos);
    sendTCPResponse(line);
    startPos = endPos + 1;
    endPos = info.indexOf('\n', startPos);
  }
  // Send last line if any
  if (startPos < info.length()) {
    sendTCPResponse(info.substring(startPos));
  }
}

bool CommandHandler::getHasNewMessage() {
  return hasNewMessage;
}

// New EEPROM command implementations
void CommandHandler::cmdGetEEPROMValue(const String& params) {
  // Get value for a specific key
  if (params.length() == 0) {
    Serial.println("[Error] No key specified");
    sendTCPResponse("[Error] No key specified");
    return;
  }

  // Example implementation - in a real implementation, you would retrieve the value from EEPROM
  String value = "";

  // Check for known configuration values
  if (params == "deviceName") {
    value = eepromManager->deviceName;
  } else if (params == "soundMQTTURL") {
    value = eepromManager->soundMQTTServerURL;
  } else if (params == "soundMQTTPort") {
    value = String(eepromManager->soundMQTTServerPort);
  } else if (params == "lightMQTTURL") {
    value = eepromManager->lightMQTTServerURL;
  } else if (params == "lightMQTTPort") {
    value = String(eepromManager->lightMQTTServerPort);
  } else if (params == "staticIPEnabled") {
    value = eepromManager->staticIPEnabled ? "Yes" : "No";
  } else if (params == "staticIP") {
    value = eepromManager->staticIP;
  } else {
    // Key not found
    Serial.println("[Error] Unknown key: " + params);
    sendTCPResponse("[Error] Unknown key: " + params);
    return;
  }

  String response = params + "=" + value;
  Serial.println(response);
  sendTCPResponse(response);
}

void CommandHandler::cmdSetEEPROMValue(const String& params) {
  // Set value for a specific key
  int colonPos = params.indexOf(':');
  if (colonPos == -1) {
    Serial.println("[Error] Invalid format! Use: setEEPROMValue:key:value");
    sendTCPResponse("[Error] Invalid format! Use: setEEPROMValue:key:value");
    return;
  }

  String key = params.substring(0, colonPos);
  String value = params.substring(colonPos + 1);

  // Set value based on the key
  bool success = false;

  // Check for known configuration values
  if (key == "deviceName") {
    eepromManager->saveDeviceName(value);
    success = true;
  } else if (key == "soundMQTTURL") {
    // Save but maintain other values
    eepromManager->saveSoundMQTTServer(
      value,
      eepromManager->soundMQTTServerPort,
      eepromManager->soundMQTTUsername,
      eepromManager->soundMQTTPassword);
    success = true;
  } else if (key == "soundMQTTPort") {
    int port = value.toInt();
    if (port > 0 && port <= 65535) {
      eepromManager->saveSoundMQTTServer(
        eepromManager->soundMQTTServerURL,
        port,
        eepromManager->soundMQTTUsername,
        eepromManager->soundMQTTPassword);
      success = true;
    } else {
      Serial.println("[Error] Invalid port value. Must be between 1 and 65535.");
      sendTCPResponse("[Error] Invalid port value. Must be between 1 and 65535.");
    }
  } else {
    // Key not recognized
    Serial.println("[Error] Unknown key: " + key);
    sendTCPResponse("[Error] Unknown key: " + key);
    return;
  }

  if (success) {
    String response = "Value saved: " + key + "=" + value;
    Serial.println(response);
    sendTCPResponse(response);
  }
}

void CommandHandler::cmdListEEPROMValues(const String& params) {
  // List all values in EEPROM
  Serial.println("\n=== EEPROM Configuration Values ===");

  // Device information
  Serial.println("deviceName=" + eepromManager->deviceName);
  sendTCPResponse("deviceName=" + eepromManager->deviceName);

  // WiFi networks
  for (int i = 0; i < 3; i++) {
    if (eepromManager->wifiCredentials[i].ssid.length() > 0) {
      Serial.println("wifi" + String(i) + "_ssid=" + eepromManager->wifiCredentials[i].ssid);
      sendTCPResponse("wifi" + String(i) + "_ssid=" + eepromManager->wifiCredentials[i].ssid);
      // Don't expose passwords
      Serial.println("wifi" + String(i) + "_password=********");
      sendTCPResponse("wifi" + String(i) + "_password=********");
    }
  }

  // Last connected network
  Serial.println("lastConnectedNetwork=" + String(eepromManager->lastConnectedNetworkIndex));
  sendTCPResponse("lastConnectedNetwork=" + String(eepromManager->lastConnectedNetworkIndex));

  // MQTT configurations
  Serial.println("soundMQTTURL=" + eepromManager->soundMQTTServerURL);
  sendTCPResponse("soundMQTTURL=" + eepromManager->soundMQTTServerURL);
  Serial.println("soundMQTTPort=" + String(eepromManager->soundMQTTServerPort));
  sendTCPResponse("soundMQTTPort=" + String(eepromManager->soundMQTTServerPort));
  Serial.println("soundMQTTUsername=" + eepromManager->soundMQTTUsername);
  sendTCPResponse("soundMQTTUsername=" + eepromManager->soundMQTTUsername);
  // Don't expose passwords
  Serial.println("soundMQTTPassword=********");
  sendTCPResponse("soundMQTTPassword=********");

  Serial.println("lightMQTTURL=" + eepromManager->lightMQTTServerURL);
  sendTCPResponse("lightMQTTURL=" + eepromManager->lightMQTTServerURL);
  Serial.println("lightMQTTPort=" + String(eepromManager->lightMQTTServerPort));
  sendTCPResponse("lightMQTTPort=" + String(eepromManager->lightMQTTServerPort));
  Serial.println("lightMQTTUsername=" + eepromManager->lightMQTTUsername);
  sendTCPResponse("lightMQTTUsername=" + eepromManager->lightMQTTUsername);
  // Don't expose passwords
  Serial.println("lightMQTTPassword=********");
  sendTCPResponse("lightMQTTPassword=********");

  // Static IP configuration
  Serial.println("staticIPEnabled=" + String(eepromManager->staticIPEnabled ? "Yes" : "No"));
  sendTCPResponse("staticIPEnabled=" + String(eepromManager->staticIPEnabled ? "Yes" : "No"));
  Serial.println("staticIP=" + eepromManager->staticIP);
  sendTCPResponse("staticIP=" + eepromManager->staticIP);
  Serial.println("staticGateway=" + eepromManager->staticGateway);
  sendTCPResponse("staticGateway=" + eepromManager->staticGateway);
  Serial.println("staticSubnet=" + eepromManager->staticSubnet);
  sendTCPResponse("staticSubnet=" + eepromManager->staticSubnet);

  Serial.println("=== End of EEPROM Values ===");
}

void CommandHandler::cmdGetDeviceName(const String& params) {
  // Return the device name
  String deviceName = eepromManager->deviceName;
  Serial.println(deviceName);
  sendTCPResponse(deviceName);
}