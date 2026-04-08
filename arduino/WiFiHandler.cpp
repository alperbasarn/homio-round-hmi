#include "WiFiHandler.h"

WiFiHandler::WiFiHandler(EEPROMManager* eepromMgr)
  : eepromManager(eepromMgr) {
  // Initialize the lastAPCheckTime
  lastAPCheckTime = millis();
  // Create the internet handler
  internetHandler = new InternetHandler(this);
  Serial.println("WiFiHandler constructed");
}

WiFiHandler::~WiFiHandler() {
  delete internetHandler;
}

void WiFiHandler::initialize() {
  Serial.println("Initializing WiFiHandler...");
  // Now start AP mode
  Serial.println("Starting AP mode during initialization...");
  startAPSTAMode();
  if (connectToWifi()) { checkInternetConnectivity(); }
  Serial.println("WiFiHandler initialized");
}

bool WiFiHandler::connectToWifi() {
  // Try to connect to last known network first
  if (eepromManager) {
    eepromManager->loadWiFiCredentials();

    int lastIndex = eepromManager->lastConnectedNetworkIndex;
    if (lastIndex >= 0 && lastIndex < 3) {
      String lastSSID = eepromManager->wifiCredentials[lastIndex].ssid;
      String lastPassword = eepromManager->wifiCredentials[lastIndex].password;

      if (!lastSSID.isEmpty()) {
        Serial.print("🔗 Attempting to reconnect to last known network: ");
        Serial.println(lastSSID);

        // Already in WIFI_AP_STA mode from constructor
        WiFi.begin(lastSSID.c_str(), lastPassword.c_str());

        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
          delay(250);  // Reduced delay for faster response
          if ((millis() - startTime) % 1000 == 0) {
            Serial.print(".");
          }
        }

        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("\n✅ Successfully reconnected to last known WiFi network!");
          Serial.print("IP Address: ");
          Serial.println(WiFi.localIP());
          // Get the current channel of the connected WiFi and use it for AP
          wifiChannel = WiFi.channel();
          Serial.print("WiFi connected on channel: ");
          Serial.println(wifiChannel);
          // Restart AP on the same channel as the connected WiFi
          restartAPWithChannel();
          wifiConnected = true;
          // Check internet connectivity
          checkInternetConnectivity();
          beginOTA(eepromManager->deviceName.c_str());
          return true;
        } else {
          Serial.println("\n❌ Failed to connect to last known WiFi network.");
        }
      }
    }
  }

  // If reconnection fails, scan for networks - but don't block too long
  Serial.println("\n🔍 Scanning for available WiFi networks...");
  int networkCount = WiFi.scanNetworks();

  if (networkCount == 0) {
    Serial.println("❌ No WiFi networks found.");
    return false;
  }

  // Find matches with stored networks
  bool foundNetwork = false;
  String selectedSSID, selectedPassword;
  int selectedIndex = -1;
  int selectedChannel = 1;

  for (int j = 0; j < 3; j++) {
    String storedSSID = eepromManager->wifiCredentials[j].ssid;
    String storedPassword = eepromManager->wifiCredentials[j].password;

    if (storedSSID.length() > 0) {
      for (int i = 0; i < networkCount; i++) {
        if (WiFi.SSID(i) == storedSSID) {
          foundNetwork = true;
          selectedSSID = storedSSID;
          selectedPassword = storedPassword;
          selectedIndex = j;
          // Remember the channel of this network
          selectedChannel = WiFi.channel(i);
          Serial.print("✅ Found saved network: ");
          Serial.print(selectedSSID);
          Serial.print(" on channel ");
          Serial.println(selectedChannel);
          break;
        }
      }
    }
    if (foundNetwork) break;
  }

  if (!foundNetwork) {
    Serial.println("❌ No matching saved networks found.");
    return false;
  }

  // Connect to the selected network
  Serial.print("🔗 Connecting to ");
  Serial.println(selectedSSID);

  WiFi.begin(selectedSSID.c_str(), selectedPassword.c_str());

  unsigned long startTime2 = millis();
  unsigned long maxConnectTime = 15000;  // 15 seconds max connect time

  while (WiFi.status() != WL_CONNECTED && millis() - startTime2 < maxConnectTime) {
    if ((millis() - startTime2) % 1000 == 0) {
      Serial.print(".");
    }
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connected to WiFi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Save last known network
    eepromManager->lastConnectedNetworkIndex = selectedIndex;
    eepromManager->saveWiFiCredentials();

    // Get the actual channel of the connected WiFi
    wifiChannel = WiFi.channel();
    Serial.print("WiFi connected on channel: ");
    Serial.println(wifiChannel);

    // Restart AP on the same channel as the connected WiFi
    restartAPWithChannel();

    wifiConnected = true;

    checkInternetConnectivity();
    beginOTA(eepromManager->deviceName.c_str());
    return true;
  } else {
    Serial.println("\n❌ Connection failed.");
    return false;
  }
}

bool WiFiHandler::startAPSTAMode() {
  // Switch to AP_STA mode to prepare for AP
  WiFi.mode(WIFI_AP_STA);
  delay(25);
  // Create AP name using device name
  String apName;
  if (eepromManager && !eepromManager->deviceName.isEmpty()) {
    apName = eepromManager->deviceName;
  } else {
    apName = String(AP_SSID) + "-" + String(random(1000));
  }

  Serial.print("📡 Starting AP with name: ");
  Serial.print(apName);
  Serial.print(", password: ");
  Serial.print(AP_PASSWORD);
  Serial.print(", channel: ");
  Serial.println(wifiChannel);

  // Call softAP but IGNORE THE RETURN VALUE - it's unreliable
  WiFi.softAP(apName.c_str(), AP_PASSWORD, wifiChannel);
  delay(25);  // Give it time to initialize

  // Configure static IP
  if (!WiFi.softAPConfig(AP_IP_ADDRESS, AP_IP_ADDRESS, IPAddress(255, 255, 255, 0))) {
    Serial.println("Note: Failed to configure AP IP address, but continuing anyway");
  }

  delay(25);

  // Check if AP is really active by getting its IP
  IPAddress apIP = WiFi.softAPIP();

  // Check if we got a valid IP (not 0.0.0.0)
  if (apIP[0] != 0) {
    Serial.println("✅ AP Mode started successfully with name: " + apName);
    Serial.println("AP IP address: " + apIP.toString());
    Serial.println("AP Channel: " + String(wifiChannel));

    apModeActive = true;
    return true;
  } else {
    Serial.println("⚠️ AP Mode may have failed to start properly");
    Serial.print("WiFi mode: ");
    Serial.println(WiFi.getMode());
    Serial.print("Current WiFi channel: ");
    Serial.println(wifiChannel);

    // Still set as active if we see the network in scan results
    apModeActive = true;  // Assume it's active even if we can't verify
    return false;
  }
}

// Check if AP is really active - optimized to reduce scanning
bool WiFiHandler::isAPActive() {
  if (!apModeActive) return false;

  // Don't do full scan here as it's expensive - just check IP
  IPAddress apIP = WiFi.softAPIP();
  if (apIP[0] != 0) {
    return true;  // AP has an IP address
  }

  // IP check failed - AP might be down
  return false;
}

// Restart AP using the same channel as WiFi
void WiFiHandler::restartAPWithChannel() {
  Serial.println("Restarting AP mode on channel " + String(wifiChannel));
  WiFi.softAPdisconnect(false);
  delay(25);
  startAPSTAMode();
}

void WiFiHandler::disconnect() {
  WiFi.disconnect();
  wifiConnected = false;
}

void WiFiHandler::beginOTA(const char* hostname) {
  // Use device name from EEPROM if available, otherwise use the provided hostname
  String otaName = hostname;
  if (eepromManager && !eepromManager->deviceName.isEmpty()) {
    otaName = eepromManager->deviceName;
  }

  Serial.println("Setting up OTA with hostname: " + otaName);
  ArduinoOTA.setHostname(otaName.c_str());

  ArduinoOTA.onStart([]() {
    String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");
}

void WiFiHandler::handleOTA() {
  ArduinoOTA.handle();
}

// Delegation to InternetHandler
bool WiFiHandler::checkInternetConnectivity() {
  return internetHandler ? internetHandler->checkInternetConnectivity() : false;
}

bool WiFiHandler::isInternetAvailable() const {
  return internetHandler ? internetHandler->isInternetAvailable() : false;
}

bool WiFiHandler::updateDateTime() {
  return internetHandler ? internetHandler->updateDateTime() : false;
}

bool WiFiHandler::updateWeather() {
  return internetHandler ? internetHandler->updateWeather() : false;
}

String WiFiHandler::getCurrentDate() const {
  return internetHandler ? internetHandler->getCurrentDate() : "----/--/--";
}

String WiFiHandler::getCurrentTime() const {
  return internetHandler ? internetHandler->getCurrentTime() : "--:--";
}
String WiFiHandler::getDayOfWeek() const {
  return internetHandler ? internetHandler->getDayOfWeek() : "---";
}
String WiFiHandler::getWeatherTemperature() const {
  return internetHandler ? internetHandler->getWeatherTemperature() : "--°C";
}

void WiFiHandler::update() {
  static unsigned long lastWiFiScanTime = 0;
  static unsigned long lastDebugTime = 0;
  unsigned long currentMillis = millis();

  // Debug output every 30 seconds
  if (currentMillis - lastDebugTime > 30000) {
    Serial.println("DEBUG: WiFiHandler::update() - WiFi status: " + String(wifiConnected ? "Connected" : "Disconnected"));
    if (wifiConnected) {
      Serial.println("DEBUG: WiFi connected to: " + WiFi.SSID() + ", IP: " + WiFi.localIP().toString() + ", RSSI: " + String(WiFi.RSSI()));
    }
    lastDebugTime = currentMillis;
  }

  // Quick check of WiFi status
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.println("WiFi reconnected to: " + WiFi.SSID());

      // Immediately check internet when WiFi connects
      Serial.println("DEBUG: WiFi connected, checking internet connectivity");
      checkInternetConnectivity();
    }
  } else if (wifiConnected) {
    wifiConnected = false;
    Serial.println("WiFi disconnected");
  }

  // Only check for WiFi networks once per minute if not connected
  if (!wifiConnected && (currentMillis - lastWiFiScanTime > WIFI_SCAN_INTERVAL)) {
    lastWiFiScanTime = currentMillis;

    // Only attempt to connect if we have stored credentials
    if (eepromManager && eepromManager->lastConnectedNetworkIndex >= 0) {
      Serial.println("DEBUG: Not connected to WiFi, scanning for known networks...");

      // Scan for networks in background if possible
      WiFi.scanNetworks(true);  // Async scan
    }
  }

  // Only check AP status periodically
  if (autoStartAP && (currentMillis - lastAPCheckTime > AP_CHECK_INTERVAL)) {
    lastAPCheckTime = currentMillis;

    // Verify AP is running
    IPAddress apIP = WiFi.softAPIP();
    if (apIP[0] == 0) {
      apModeActive = false;
      Serial.println("AP mode not active. Restarting it...");
      startAPSTAMode();
    } else if (!apModeActive) {
      apModeActive = true;
    }
  }

  // Update internet components
  if (internetHandler) {
    internetHandler->update();
  } else {
    Serial.println("ERROR: internetHandler is NULL in WiFiHandler::update()");
  }
  // Handle OTA if WiFi is connected
  if (wifiConnected) {
    ArduinoOTA.handle();
  }
}