#include "TCPHandler.h"

TCPHandler::TCPHandler(WiFiHandler* wifiHdlr)
  : wifiHandler(wifiHdlr), tcpServer(nullptr), soundServerPort(12345), 
    lightServerPort(12345), hasNewTCPMessage(false) {
  
  soundServerIP = "";
  lightServerIP = "";
  
  // Initialize default static IP configuration
  useStaticIP = false;
  staticIP = IPAddress().fromString(DEFAULT_STATIC_IP);
  gateway = IPAddress().fromString(DEFAULT_GATEWAY);
  subnetMask = IPAddress().fromString(DEFAULT_SUBNET);
  dns1 = IPAddress().fromString(DEFAULT_DNS1);
  dns2 = IPAddress().fromString(DEFAULT_DNS2);
}

TCPHandler::~TCPHandler() {
  if (tcpServer) {
    tcpServer->stop();
    delete tcpServer;
    tcpServer = nullptr;
  }
}

void TCPHandler::startTCPServer(int port) {
  // Clean up any existing server
  if (tcpServer) {
    tcpServer->stop();
    delete tcpServer;
    tcpServer = nullptr;
  }
  
  // If static IP is enabled, apply configuration to WiFi
  if (useStaticIP) {
    if (wifiHandler->isAPModeActive()) {
      // For AP mode, set the softAP IP
      bool configResult = WiFi.softAPConfig(staticIP, gateway, subnetMask);
      Serial.println("Configuring AP with static IP: " + staticIP.toString() + 
                   ", Result: " + String(configResult ? "Success" : "Failure"));
    } else if (!wifiHandler->isWiFiConnected()) {
      // For station mode, apply the static IP configuration
      Serial.println("Setting static IP for station mode: " + staticIP.toString());
      bool configResult = WiFi.config(staticIP, gateway, subnetMask, dns1, dns2);
      Serial.println("Static IP configuration result: " + String(configResult ? "Success" : "Failure"));
    }
  }
  
  // Create and start a new server
  tcpServer = new WiFiServer(port);
  tcpServer->begin();
  
  // Log server start with the IP
  IPAddress serverIP = getServerIP();
  Serial.println("TCP server started on " + serverIP.toString() + ":" + String(port));
}

void TCPHandler::handleTCPServer() {
  if (!tcpServer) {
    return;
  }
  
  // Check if there's a new client connection
  if (tcpServer->hasClient()) {
    // If we already have a client, disconnect it
    if (tcpServerClient) {
      tcpServerClient.stop();
      Serial.println("Previous client disconnected");
    }

    // Accept the new client
    tcpServerClient = tcpServer->available();
    Serial.println("New client connected");

    // Send welcome message
    tcpServerClient.println("ESP32 Configuration Server");
    tcpServerClient.println("Type 'help' for available commands");
    
    // Send current IP address
    IPAddress currentIP = getServerIP();
    tcpServerClient.println("IP: " + currentIP.toString());
  }

  // Only check for data if client exists and is connected
  if (!tcpServerClient || !tcpServerClient.connected()) {
    return;
  }

  // Check if data is available once - avoids repeated function calls
  int availableBytes = tcpServerClient.available();
  if (availableBytes <= 0) {
    return;
  }

  // Process up to 3 commands per call to avoid blocking too long
  int commandsProcessed = 0;
  while (tcpServerClient.available() > 0 && commandsProcessed < 3) {
    // Use readStringUntil but with a timeout to avoid blocking
    String command = tcpServerClient.readStringUntil('\n');
    command.trim();

    if (command.length() > 0) {
      Serial.println("Received TCP command: " + command);
      tcpReceivedCommand = command;
      hasNewTCPMessage = true;

      // Echo the command back to the client
      tcpServerClient.println("Received: " + command);
      
      commandsProcessed++;
    }
    
    // Small yield to allow other processing
    yield();
  }
}

bool TCPHandler::getHasNewMessage() {
  return hasNewTCPMessage;
}

String TCPHandler::getMessage() {
  hasNewTCPMessage = false;
  return tcpReceivedCommand;
}

void TCPHandler::sendTCPResponse(const String& message) {
  if (tcpServerClient && tcpServerClient.connected()) {
    tcpServerClient.println(message);
    Serial.println("TCP Response: " + message);
  }
}

bool TCPHandler::connectToServer(const String& ip, uint16_t port) {
  if (!wifiHandler->isWiFiConnected()) {
    Serial.println("Cannot connect to server: WiFi not connected");
    return false;
  }
  
  if (ip.isEmpty()) {
    Serial.println("Cannot connect to server: IP address is empty");
    return false;
  }

  Serial.println("Connecting to server at: " + ip + ":" + String(port));
  
  // Try to connect with timeout
  unsigned long startTime = millis();
  while (!client.connect(ip.c_str(), port)) {
    if (millis() - startTime > 5000) {  // 5-second timeout
      Serial.println("Server connection timed out.");
      return false;
    }
    Serial.print(".");
    delay(500);
  }
  
  Serial.println("\nServer connected.");
  return true;
}

bool TCPHandler::connectToSoundServer() {
  return connectToServer(soundServerIP, soundServerPort);
}

bool TCPHandler::connectToLightServer() {
  return connectToServer(lightServerIP, lightServerPort);
}

void TCPHandler::sendData(const String& data) {
  if (client.connected()) {
    Serial.println("Sending data: " + data);
    client.print(data + "\n");
  } else {
    Serial.println("Error: Client is not connected.");
  }
}

String TCPHandler::receiveData() {
  if (client.connected() && client.available()) {
    String data = client.readStringUntil('\n');
    data.trim();
    Serial.println("Received data: " + data);
    return data;
  }
  return "";
}

void TCPHandler::disconnect() {
  if (client.connected()) {
    client.stop();
  }
}

bool TCPHandler::isServerConnected() {
  return client.connected();
}

void TCPHandler::setServerInfo(const String& soundIP, uint16_t soundPort, 
                              const String& lightIP, uint16_t lightPort) {
  soundServerIP = soundIP;
  soundServerPort = soundPort;
  lightServerIP = lightIP;
  lightServerPort = lightPort;
  
  Serial.println("TCP Server Info updated:");
  Serial.println("  Sound Server: " + soundIP + ":" + String(soundPort));
  Serial.println("  Light Server: " + lightIP + ":" + String(lightPort));
}

// Set static IP configuration
void TCPHandler::setStaticIP(bool enable, const IPAddress& ip, const IPAddress& gw, 
                           const IPAddress& subnet, const IPAddress& dns1, const IPAddress& dns2) {
  useStaticIP = enable;
  staticIP = ip;
  gateway = gw;
  subnetMask = subnet;
  this->dns1 = dns1;
  this->dns2 = dns2;
  
  Serial.println("Static IP configuration " + String(enable ? "enabled" : "disabled") + ":");
  Serial.println("  IP: " + ip.toString());
  Serial.println("  Gateway: " + gw.toString());
  Serial.println("  Subnet: " + subnet.toString());
  Serial.println("  DNS1: " + dns1.toString());
  Serial.println("  DNS2: " + dns2.toString());
}

// Set static IP configuration from strings
void TCPHandler::setStaticIPFromStrings(bool enable, const String& ip, const String& gw, 
                                      const String& subnet, const String& dns1, const String& dns2) {
  IPAddress ipAddr, gwAddr, subnetAddr, dns1Addr, dns2Addr;
  
  // Convert strings to IPAddress objects
  bool validIP = ipAddr.fromString(ip) && 
                gwAddr.fromString(gw) && 
                subnetAddr.fromString(subnet) && 
                dns1Addr.fromString(dns1) && 
                dns2Addr.fromString(dns2);
  
  if (!validIP) {
    Serial.println("Error: Invalid IP address format");
    return;
  }
  
  // Set the configuration
  setStaticIP(enable, ipAddr, gwAddr, subnetAddr, dns1Addr, dns2Addr);
}

// Apply configuration from EEPROMManager
void TCPHandler::applyEEPROMConfig(bool enabled, const String& ip, const String& gateway, 
                                 const String& subnet, const String& dns1, const String& dns2) {
  // Apply the static IP configuration
  setStaticIPFromStrings(enabled, ip, gateway, subnet, dns1, dns2);
}

// Get current server IP
IPAddress TCPHandler::getServerIP() const {
  // If static IP is enabled, return the configured IP
  if (useStaticIP) {
    return staticIP;
  }
  
  // Otherwise get the IP from WiFi
  if (wifiHandler) {
    if (wifiHandler->isWiFiConnected()) {
      return WiFi.localIP();
    } else if (wifiHandler->isAPModeActive()) {
      return WiFi.softAPIP();
    }
  }
  
  // If all fails, return default
  return IPAddress().fromString(DEFAULT_STATIC_IP);
}

// Get static IP configuration as string
String TCPHandler::getStaticIPConfig() const {
  String config = "Static IP Configuration:\n";
  config += "  Enabled: " + String(useStaticIP ? "Yes" : "No") + "\n";
  config += "  IP: " + staticIP.toString() + "\n";
  config += "  Gateway: " + gateway.toString() + "\n";
  config += "  Subnet: " + subnetMask.toString() + "\n";
  config += "  DNS1: " + dns1.toString() + "\n";
  config += "  DNS2: " + dns2.toString() + "\n";
  
  return config;
}

// Optimized update method
void TCPHandler::update() {
  // If there's no server, nothing to do
  if (!tcpServer) return;
  
  // Only check for new client if we don't already have one
  if (!tcpServerClient || !tcpServerClient.connected()) {
    if (tcpServer->hasClient()) {
      // Accept the new client
      tcpServerClient = tcpServer->available();
      Serial.println("New client connected");

      // Send welcome message
      tcpServerClient.println("ESP32 Configuration Server");
      tcpServerClient.println("Type 'help' for available commands");
      
      // Send current IP address
      IPAddress currentIP = getServerIP();
      tcpServerClient.println("IP: " + currentIP.toString());
    }
  } else {
    // Fast check for available data
    int availableBytes = tcpServerClient.available();
    if (availableBytes > 0) {
      // Read one command per cycle to avoid blocking
      String command = tcpServerClient.readStringUntil('\n');
      command.trim();

      if (command.length() > 0) {
        tcpReceivedCommand = command;
        hasNewTCPMessage = true;

        // Echo the command back to the client
        tcpServerClient.println("Received: " + command);
      }
    }
  }
}