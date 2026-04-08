#ifndef EEPROM_MANAGER_H
#define EEPROM_MANAGER_H
#include <Arduino.h>
#include <EEPROM.h>

// EEPROM Size and Configuration
#define EEPROM_SIZE 1024                  // Increased EEPROM size
#define NUM_WIFI_CREDENTIALS 3            // Number of stored WiFi credentials
#define MAX_SSID_LENGTH 32                // Max length for SSID (including null terminator)
#define MAX_PASS_LENGTH 64                // Max length for password (including null terminator)
#define MAX_IP_LENGTH 16                  // Max length for IP address (including null terminator)
#define MAX_URL_LENGTH 64                 // Max length for URL
#define MAX_USERNAME_LENGTH 32            // Max length for username
#define MAX_DEVICE_NAME_LENGTH 32         // Max length for device name

// EEPROM Address Definitions
// Each credential occupies: MAX_SSID_LENGTH + MAX_PASS_LENGTH + 1 (for the remember flag)
#define CREDENTIAL_BLOCK_SIZE (MAX_SSID_LENGTH + MAX_PASS_LENGTH + 1)
// The WiFi credentials start at address 0
#define WIFI_CREDENTIALS_START 0
// The last connected network index is stored right after the credentials
#define LAST_CONNECTED_ADDRESS (WIFI_CREDENTIALS_START + NUM_WIFI_CREDENTIALS * CREDENTIAL_BLOCK_SIZE)
// Sound TCP Server configuration
#define SOUND_TCP_SERVER_IP_ADDRESS (LAST_CONNECTED_ADDRESS + 4)
#define SOUND_TCP_SERVER_PORT_ADDRESS (SOUND_TCP_SERVER_IP_ADDRESS + MAX_IP_LENGTH)
// Light TCP Server configuration
#define LIGHT_TCP_SERVER_IP_ADDRESS (SOUND_TCP_SERVER_PORT_ADDRESS + 4)
#define LIGHT_TCP_SERVER_PORT_ADDRESS (LIGHT_TCP_SERVER_IP_ADDRESS + MAX_IP_LENGTH)
// Sound MQTT Server configuration
#define SOUND_MQTT_SERVER_URL_ADDRESS (LIGHT_TCP_SERVER_PORT_ADDRESS + 4)
#define SOUND_MQTT_SERVER_PORT_ADDRESS (SOUND_MQTT_SERVER_URL_ADDRESS + MAX_URL_LENGTH)
#define SOUND_MQTT_SERVER_USERNAME_ADDRESS (SOUND_MQTT_SERVER_PORT_ADDRESS + 4)
#define SOUND_MQTT_SERVER_PASSWORD_ADDRESS (SOUND_MQTT_SERVER_USERNAME_ADDRESS + MAX_USERNAME_LENGTH)
// Light MQTT Server configuration
#define LIGHT_MQTT_SERVER_URL_ADDRESS (SOUND_MQTT_SERVER_PASSWORD_ADDRESS + MAX_PASS_LENGTH)
#define LIGHT_MQTT_SERVER_PORT_ADDRESS (LIGHT_MQTT_SERVER_URL_ADDRESS + MAX_URL_LENGTH)
#define LIGHT_MQTT_SERVER_USERNAME_ADDRESS (LIGHT_MQTT_SERVER_PORT_ADDRESS + 4)
#define LIGHT_MQTT_SERVER_PASSWORD_ADDRESS (LIGHT_MQTT_SERVER_USERNAME_ADDRESS + MAX_USERNAME_LENGTH)
// Device name
#define DEVICE_NAME_ADDRESS (LIGHT_MQTT_SERVER_PASSWORD_ADDRESS + MAX_PASS_LENGTH)

// Static IP Configuration addresses (new)
#define STATIC_IP_ENABLED_ADDRESS (DEVICE_NAME_ADDRESS + MAX_DEVICE_NAME_LENGTH)
#define STATIC_IP_ADDRESS (STATIC_IP_ENABLED_ADDRESS + 1)
#define STATIC_GATEWAY_ADDRESS (STATIC_IP_ADDRESS + MAX_IP_LENGTH)
#define STATIC_SUBNET_ADDRESS (STATIC_GATEWAY_ADDRESS + MAX_IP_LENGTH)
#define STATIC_DNS1_ADDRESS (STATIC_SUBNET_ADDRESS + MAX_IP_LENGTH)
#define STATIC_DNS2_ADDRESS (STATIC_DNS1_ADDRESS + MAX_IP_LENGTH)

struct WifiCredential {
  String ssid;
  String password;
  bool remember;
};

class EEPROMManager {
public:
  WifiCredential wifiCredentials[NUM_WIFI_CREDENTIALS];
  int lastConnectedNetworkIndex;
  
  // TCP Server configurations
  String soundTCPServerIP;
  int soundTCPServerPort;
  String lightTCPServerIP;
  int lightTCPServerPort;
  
  // MQTT Server configurations
  String soundMQTTServerURL;
  int soundMQTTServerPort;
  String soundMQTTUsername;
  String soundMQTTPassword;
  
  String lightMQTTServerURL;
  int lightMQTTServerPort;
  String lightMQTTUsername;
  String lightMQTTPassword;
  
  String deviceName;

  // Static IP Configuration (new)
  bool staticIPEnabled;
  String staticIP;
  String staticGateway;
  String staticSubnet;
  String staticDNS1;
  String staticDNS2;

  EEPROMManager();
  void begin();
  void saveWiFiCredentials();
  void loadWiFiCredentials();
  void clearEntireEEPROM();
  
  // New configuration methods
  void saveSoundTCPServer(const String& ip, int port);
  void saveLightTCPServer(const String& ip, int port);
  void saveSoundMQTTServer(const String& url, int port, const String& username, const String& password);
  void saveLightMQTTServer(const String& url, int port, const String& username, const String& password);
  void saveDeviceName(const String& name);
  
  // Static IP configuration methods (new)
  void saveStaticIPConfig(bool enabled, const String& ip, const String& gateway, 
                         const String& subnet, const String& dns1, const String& dns2);
  void loadStaticIPConfig();
  
  // Load configurations
  void loadAllConfigurations();
  
  // Get configuration as string for display
  String getConfigurationInfo();
  String getStaticIPInfo(); // New method to get static IP info

private:
  void writeStringToEEPROM(int address, const String &str, int maxLength);
  String readStringFromEEPROM(int address, int maxLength);
  void writeIntToEEPROM(int address, int value);
  int readIntFromEEPROM(int address, int defaultValue);
};

#endif