#ifndef TCP_HANDLER_H
#define TCP_HANDLER_H

#include <WiFi.h>
#include <WiFiClient.h>
#include "WiFiHandler.h"

// Default values for static IP configuration
#define DEFAULT_STATIC_IP "192.168.4.1"
#define DEFAULT_GATEWAY "192.168.4.1"
#define DEFAULT_SUBNET "255.255.255.0"
#define DEFAULT_DNS1 "8.8.8.8"
#define DEFAULT_DNS2 "8.8.4.4"

class TCPHandler {
private:
  WiFiHandler* wifiHandler;            // Reference to WiFi handler
  WiFiClient client;                   // TCP client for server connections
  WiFiServer* tcpServer = nullptr;     // TCP server for AP mode
  WiFiClient tcpServerClient;          // Current TCP server client connection
  String tcpReceivedCommand;           // Buffer for received TCP command
  bool hasNewTCPMessage = false;       // Flag for new TCP message
  
  // Server parameters
  String soundServerIP;
  uint16_t soundServerPort;
  String lightServerIP;
  uint16_t lightServerPort;
  
  // Static IP configuration
  bool useStaticIP = false;            // Flag to enable/disable static IP
  IPAddress staticIP;                  // Static IP address
  IPAddress gateway;                   // Gateway IP address
  IPAddress subnetMask;                // Subnet mask
  IPAddress dns1;                      // Primary DNS server
  IPAddress dns2;                      // Secondary DNS server
  
public:
  TCPHandler(WiFiHandler* wifiHandler);
  ~TCPHandler();

  // TCP server methods
  void startTCPServer(int port);       // Start TCP server on specified port
  void handleTCPServer();              // Handle incoming TCP connections and data
  bool getHasNewMessage();             // Check if there's a new TCP message
  String getMessage();                 // Get the received TCP message
  void sendTCPResponse(const String& message); // Send response to connected TCP client
  
  // TCP client methods
  bool connectToServer(const String& ip, uint16_t port); // Connect to TCP server
  bool connectToSoundServer();         // Connect to configured sound server
  bool connectToLightServer();         // Connect to configured light server
  void sendData(const String& data);   // Send data to connected server
  String receiveData();                // Receive data from connected server
  void disconnect();                   // Disconnect from TCP server
  bool isServerConnected();            // Check if connected to TCP server
  
  // Configuration methods
  void setServerInfo(const String& soundIP, uint16_t soundPort, 
                    const String& lightIP, uint16_t lightPort);
  
  // Static IP methods
  void setStaticIP(bool enable, const IPAddress& ip, const IPAddress& gw, 
                  const IPAddress& subnet, const IPAddress& dns1 = IPAddress(8, 8, 8, 8), 
                  const IPAddress& dns2 = IPAddress(8, 8, 4, 4));
  void setStaticIPFromStrings(bool enable, const String& ip, const String& gw, 
                             const String& subnet, const String& dns1 = "8.8.8.8", 
                             const String& dns2 = "8.8.4.4");
  bool isUsingStaticIP() const { return useStaticIP; }
  IPAddress getServerIP() const;      // Get current server IP
  String getStaticIPConfig() const;   // Get static IP configuration as string
  
  // Apply configuration from EEPROMManager
  void applyEEPROMConfig(bool enabled, const String& ip, const String& gateway, 
                        const String& subnet, const String& dns1, const String& dns2);
  
  // Main update method
  void update();                       // Handle periodic TCP tasks
};

#endif // TCP_HANDLER_H