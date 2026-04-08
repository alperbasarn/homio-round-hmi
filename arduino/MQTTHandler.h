#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <WiFiClientSecure.h>
// Increase MQTT packet size before including PubSubClient
#define MQTT_MAX_PACKET_SIZE 1024
#include <PubSubClient.h>
#include "WiFiHandler.h"
#include "EEPROMManager.h"

// MQTT Configuration
#define MQTT_CONNECTION_TIMEOUT 10000  // 10 seconds timeout for MQTT connections
#define MQTT_RECONNECT_DELAY 5000      // 5 seconds between reconnection attempts

class MQTTHandler {
private:
  WiFiHandler* wifiHandler;            // Reference to WiFi handler
  EEPROMManager* eepromManager;        // Reference to EEPROM manager
  
  WiFiClientSecure wifiClient;         // Secure WiFi client for MQTT
  PubSubClient mqttClient;             // MQTT client object
  bool connecting = false;
  
  // MQTT Configuration
  String mqtt_broker;
  int mqtt_port;
  String mqtt_username;
  String mqtt_password;
  bool useLightConfig = false;         // Whether using light or sound MQTT config
  bool initialized=false;
  
  // Operation tracking
  unsigned long lastMQTTSentTime = 0;
  unsigned long lastMQTTConnectAttempt = 0;
  
  // Sound message handling
  String soundMessage;                 // Latest sound message content
  bool hasSoundMessage = false;        // Flag for new sound message
  
  // Helper methods
  void logMQTTState(int state);        // Log MQTT connection state
  
  // Callback method for MQTT messages
  static void staticCallback(char* topic, byte* payload, unsigned int length, void* object);
  void callback(char* topic, byte* payload, unsigned int length);
  
public:
  MQTTHandler(WiFiHandler* wifiHandler, EEPROMManager* eepromMgr);
  ~MQTTHandler();

  // MQTT methods
  void initializeMQTT(bool useLightConfig = false); // Initialize MQTT with stored credentials
  bool connectToMQTTServer();          // Connect to MQTT broker
  void sendMQTTMessage(const char* topic, const String& message); // Send MQTT message
  bool isMQTTConnected();              // Check if MQTT is connected
  
  // Sound message methods
  bool getHasSoundMessage();           // Check if a new sound message is available (resets flag)
  String getSoundMessage();            // Get the content of the sound message
  String name;
  
  // Configuration check methods
  bool hasSoundMQTTConfigured();       // Check if Sound MQTT is configured
  bool hasLightMQTTConfigured();       // Check if Light MQTT is configured
  
  // Main update method
  void update();                      // Maintain MQTT connection and process messages
};

#endif // MQTT_HANDLER_H