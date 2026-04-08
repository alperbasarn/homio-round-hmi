#ifndef WIFI_TCP_CLIENT_H
#define WIFI_TCP_CLIENT_H

#include "WiFiHandler.h"
#include "TCPHandler.h"
#include "MQTTHandler.h"
#include "EEPROMManager.h"

class WiFiTCPClient {
private:
  EEPROMManager* eepromManager;  // EEPROM manager - owned externally
  WiFiHandler* wifiHandler;      // WiFi connection handler
  TCPHandler* tcpHandler;        // TCP communication handler
  MQTTHandler* mqttHandler;      // MQTT communication handler

  unsigned long intervalMs;  // Interval for periodic operations

public:
  WiFiTCPClient(EEPROMManager* eepromMgr, unsigned long mqttInterval = 5000);
  ~WiFiTCPClient();
  int getWeatherHumidity();
  float getWeatherWindSpeed();
  String getWeatherCondition();
  // Complete initialization method for all network components
  void initialize();

  // Methods delegated to handlers - maintaining the original interface

  // WiFi methods
  bool connectToWifi();                 // Connect to Wi-Fi using stored credentials
  bool startAPMode();                   // Start AP mode for configuration
  bool isWiFiConnected() const;         // Check Wi-Fi connection status
  bool isAPModeActive() const;          // Check if AP mode is active
  bool isInternetAvailable() const;     // Check if internet is available
  void beginOTA(const char* hostname);  // Initialize OTA
  int getWiFiSignalStrength();          // Get WiFi signal strength (0-3)

  // TCP methods (keeping for backward compatibility)
  void sendTCPResponse(const String& message);  // Send response to TCP client
  bool getHasNewMessage();                      // Check if there's a new TCP message
  String getMessage();                          // Get the received TCP message
  bool connectToSoundServer();                  // Connect to the sound server
  bool connectToLightServer();                  // Connect to the light server
  void sendData(const String& data);            // Send data to the server
  String receiveData();                         // Receive data from the server
  void disconnect();                            // Disconnect from the server
  bool isServerConnected();                     // Check server connection status

  // MQTT methods
  void initializeMQTT();                                           // Initialize MQTT with stored credentials
  bool connectToMQTTServer();                                      // Connect to MQTT broker
  void sendMQTTMessage(const char* topic, const String& message);  // Send MQTT message
  bool hasSoundMQTTConfigured();                                   // Check if Sound MQTT is configured
  bool hasLightMQTTConfigured();                                   // Check if Light MQTT is configured
  bool isMQTTConnected();                                          // Check if MQTT is connected
  
  // New methods for sound message handling
  bool getHasSoundMessage();                                       // Check for new sound messages
  String getSoundMessage();                                        // Get the sound message content

  // Time and weather methods
  String getCurrentDate();  // Get current date
  String getCurrentTime();  // Get current time
  String getDayOfWeek();
  String getWeatherTemperature();  // Get weather temperature

  // Load configuration and update
  void loadConfiguration();  // Load configuration from EEPROM
  void update();             // Run periodic updates on all handlers
  
  // Access to handlers
  MQTTHandler* getMQTTHandler() { return mqttHandler; }  // Get access to the MQTT handler
  WiFiHandler* getWiFiHandler() { return wifiHandler; }  // Get access to the WiFi handler
};

#endif  // WIFI_TCP_CLIENT_H