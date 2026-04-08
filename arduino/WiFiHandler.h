#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include <WiFi.h>
#include <ArduinoOTA.h>
#include "EEPROMManager.h"
#include "InternetHandler.h" // Include the new InternetHandler

// Configuration values
#define AP_SSID "ESP32-Setup"
#define AP_PASSWORD "knobsetup"  // Password for AP mode
#define AP_IP_ADDRESS IPAddress(192, 168, 4, 1)
#define AP_SERVER_PORT 23  // Default telnet port

// Checking intervals
#define AP_CHECK_INTERVAL 30000       // Check AP status every 30 seconds
#define WIFI_SCAN_INTERVAL 60000      // Scan for networks once per minute

class WiFiHandler {
private:
    EEPROMManager* eepromManager;        // Reference to EEPROM manager
    InternetHandler* internetHandler;    // Handles internet, time and weather
    
    bool wifiConnected = false;          // WiFi connection status
    bool apModeActive = false;           // AP mode status
    unsigned long lastAPCheckTime = 0;   // Time of last AP status check
    bool autoStartAP = true;             // Flag to auto-start AP mode when WiFi connection fails
    int wifiChannel = 1;                 // WiFi channel (must be the same for AP and STA modes)

public:
    WiFiHandler(EEPROMManager* eepromMgr);
    ~WiFiHandler();

    // Initialization method - call this before using
    void initialize();                   // Initialize WiFi and start AP mode

    // WiFi connection methods
    bool connectToWifi();                // Connect to WiFi using stored credentials
    bool startAPSTAMode();                  // Start Access Point mode for configuration
    void restartAPWithChannel();         // Restart AP mode using the current WiFi channel
    void disconnect();                   // Disconnect from WiFi
    
    // Status methods
    bool isWiFiConnected() const { return wifiConnected; }
    bool isAPModeActive() const { return apModeActive; }
    bool isAPActive();                   // Check if AP is really active by scanning
    int getWiFiSignalStrength();         // Get WiFi signal strength (0-3)
    
    // Configuration methods
    void setAutoStartAP(bool enable) { autoStartAP = enable; }

    // Internet connectivity methods (delegated to InternetHandler)
    bool checkInternetConnectivity(); 
    bool isInternetAvailable() const;
    
    // OTA update methods
    void beginOTA(const char* hostname); // Initialize OTA updates
    void handleOTA();                    // Handle OTA update process
    
    // Time and weather methods (delegated to InternetHandler)
    bool updateDateTime();
    bool updateWeather();
    String getCurrentDate() const;
    String getCurrentTime() const;
    String getDayOfWeek() const;
    String getWeatherTemperature() const;
    
    // Main update method - call this regularly
    void update();
    
    // Getter for the InternetHandler
    InternetHandler* getInternetHandler() const { return internetHandler; }
};

#endif // WIFI_HANDLER_H