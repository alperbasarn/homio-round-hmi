#include "CommandHandler.h"
#include "DisplayController.h"
#include "NVSManager.h"
#include "KnobController.h"
#include "WiFiManager.h"
#include "MQTTManager.h"
#include "MediaController.h"
#include "OTAManager.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static const char *TAG = "CommandHandler";

// File descriptor for stdin (console input)
static int stdin_fd = -1;

CommandHandler::CommandHandler(DisplayController* dc, NVSManager* nvs, WiFiManager* wifi, MQTTManager* mqtt)
    : displayController(dc),
      nvsManager(nvs),
      knobController(nullptr),
      wifiManager(wifi),
      mqttManager(mqtt),
      mediaController(nullptr),
      otaManager(nullptr) {
}

void CommandHandler::begin() {
    // Use stdin (file descriptor 0) for console input - works for both UART and USB Serial/JTAG
    // Set stdin to non-blocking mode
    stdin_fd = fileno(stdin);
    if (stdin_fd >= 0) {
        int flags = fcntl(stdin_fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(stdin_fd, F_SETFL, flags | O_NONBLOCK);
            ESP_LOGI(TAG, "Console input configured (non-blocking stdin)");
        } else {
            ESP_LOGW(TAG, "Failed to get stdin flags");
        }
    } else {
        ESP_LOGW(TAG, "Failed to get stdin file descriptor");
    }

    registerCommands();
    ESP_LOGI(TAG, "Command handler initialized.");
}

void CommandHandler::registerKnobController(KnobController* kc) {
    knobController = kc;
}

void CommandHandler::registerMediaController(MediaController* mc) {
    mediaController = mc;
}

void CommandHandler::registerOTAManager(OTAManager* ota) {
    otaManager = ota;
}

void CommandHandler::update() {
    // Process serial commands from console (stdin)
    char data[128];
    int length = 0;

    if (stdin_fd >= 0) {
        length = read(stdin_fd, data, sizeof(data) - 1);
        if (length < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // Real error, not just "no data available"
            length = 0;
        } else if (length < 0) {
            length = 0;  // No data available (non-blocking)
        }
    }

    if (length > 0) {
        data[length] = '\0';  // Null-terminate for safety
        commandFromPC.append((char*)data, length);

        // Support both CR and LF line endings and process all full lines.
        while (true) {
            size_t lf_pos = commandFromPC.find('\n');
            size_t cr_pos = commandFromPC.find('\r');
            size_t line_end = std::string::npos;
            if (lf_pos != std::string::npos && cr_pos != std::string::npos) {
                line_end = std::min(lf_pos, cr_pos);
            } else if (lf_pos != std::string::npos) {
                line_end = lf_pos;
            } else if (cr_pos != std::string::npos) {
                line_end = cr_pos;
            }
            if (line_end == std::string::npos) {
                break;
            }

            std::string command = commandFromPC.substr(0, line_end);
            commandFromPC.erase(0, line_end + 1);
            while (!commandFromPC.empty() && (commandFromPC[0] == '\r' || commandFromPC[0] == '\n')) {
                commandFromPC.erase(0, 1);
            }

            command = trimCommand(command);
            if (!command.empty()) {
                processCommand(command);
            }
        }
    }

    // Process knob commands
    if (knobController && knobController->getHasNewMessage()) {
        const std::string command = trimCommand(knobController->getReceivedCommand());
        if (!command.empty()) {
            processCommand(command);
        }
    }
}

void CommandHandler::handleExternalCommand(const std::string& commandLine) {
    const std::string command = trimCommand(commandLine);
    if (command.empty()) {
        ESP_LOGW(TAG, "Ignoring empty external command");
        return;
    }
    ESP_LOGI(TAG, "External command: %s", command.c_str());
    processCommand(command);
}

void CommandHandler::processCommand(const std::string& command_line) {
    ESP_LOGI(TAG, "Processing command: %s", command_line.c_str());
    std::string command;
    std::string params;
    size_t separator_pos = command_line.find(':');
    if (separator_pos != std::string::npos) {
        command = command_line.substr(0, separator_pos);
        params = command_line.substr(separator_pos + 1);
    } else {
        command = command_line;
    }

    auto it = commands.find(command);
    if (it != commands.end()) {
        it->second.callback(params);
    } else {
        ESP_LOGE(TAG, "Unknown command: %s", command.c_str());
    }
}

std::string CommandHandler::trimCommand(const std::string& command) {
    std::string result = command;
    result.erase(result.begin(), std::find_if(result.begin(), result.end(), [](unsigned char c) {
        return !std::isspace(c);
    }));
    result.erase(std::find_if(result.rbegin(), result.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(), result.end());
    return result;
}

void CommandHandler::publishResponse(const std::string& response) {
    if (!mqttManager || !mqttManager->isConnected()) {
        return;
    }
    if (response.empty()) {
        return;
    }
    mqttManager->publish(MQTT_TOPIC_COMMAND_RESPONSE, response);
}

void CommandHandler::registerCommands() {
    commands["+"] = {"+", [this](const std::string& p) { this->cmdIncrementSetpoint(p); }, "Increment setpoint"};
    commands["-"] = {"-", [this](const std::string& p) { this->cmdDecrementSetpoint(p); }, "Decrement setpoint"};
    commands["reset"] = {"reset", [this](const std::string& p) { this->cmdReset(p); }, "Restart ESP"};
    commands["home"] = {"home", [this](const std::string& p) { this->cmdSwitchToHome(p); }, "Switch to home page"};
    commands["info"] = {"info", [this](const std::string& p) { this->cmdSwitchToInfo(p); }, "Switch to info page"};
    commands["clearNVS"] = {"clearNVS", [this](const std::string& p) { this->cmdClearNVS(p); }, "Remove all saved data from NVS"};
    commands["connectWifi"] = {"connectWifi", [this](const std::string& p) { this->cmdConnectWifi(p); }, "connectWifi:SSID:Password - Save and connect to Wi-Fi"};
    commands["showNetworks"] = {"showNetworks", [this](const std::string& p) { this->cmdShowNetworks(p); }, "Show stored Wi-Fi networks"};
    commands["calibrateOrientation"] = {"calibrateOrientation", [this](const std::string& p) { this->cmdCalibrateOrientation(p); }, "Calibrate device orientation"};
    commands["setpoint"] = {"setpoint", [this](const std::string& p) { this->cmdKnobSetpoint(p); }, "Set the setpoint value"};
    commands["getDeviceName"] = {"getDeviceName", [this](const std::string& p) { this->cmdGetDeviceName(p); }, "Get the device name"};
    commands["setDeviceName"] = {"setDeviceName", [this](const std::string& p) { this->cmdSetDeviceName(p); }, "setDeviceName:name - Set device name"};
    commands["configureMQTTServer"] = {"configureMQTTServer", [this](const std::string& p) { this->cmdConfigureMQTTServer(p); }, "configureMQTTServer:Host:Port[:User:Pass] - Configure MQTT Server"};
    commands["commInfo"] = {"commInfo", [this](const std::string& p) { this->cmdCommInfo(p); }, "Display all communication configuration information"};
    commands["configureStaticIP"] = {"configureStaticIP", [this](const std::string& p) { this->cmdConfigureStaticIP(p); }, "configureStaticIP:IP:Gateway:Subnet:DNS1:DNS2 - Configure static IP settings"};
    commands["enableStaticIP"] = {"enableStaticIP", [this](const std::string& p) { this->cmdEnableStaticIP(p); }, "Enable static IP configuration"};
    commands["disableStaticIP"] = {"disableStaticIP", [this](const std::string& p) { this->cmdDisableStaticIP(p); }, "Disable static IP configuration (use DHCP)"};
    commands["showStaticIP"] = {"showStaticIP", [this](const std::string& p) { this->cmdShowStaticIP(p); }, "Show current static IP configuration"};
    commands["listNVSValues"] = {"listNVSValues", [this](const std::string& p) { this->cmdListNVSValues(p); }, "List all NVS values"};
    commands["startSoundRecord"] = {"startSoundRecord", [this](const std::string& p) { this->cmdStartSoundRecord(p); }, "startSoundRecord[:filename] - Start recording audio to SD"};
    commands["stopSoundRecord"] = {"stopSoundRecord", [this](const std::string& p) { this->cmdStopSoundRecord(p); }, "stopSoundRecord - Stop active audio recording"};
    commands["playLastSoundRecord"] = {"playLastSoundRecord", [this](const std::string& p) { this->cmdPlayLastSoundRecord(p); }, "playLastSoundRecord - Play the most recent recording"};
    commands["otaUpdate"] = {"otaUpdate", [this](const std::string& p) { this->cmdOTAUpdate(p); }, "otaUpdate:URL - Start OTA update"};
    commands["otaInfo"] = {"otaInfo", [this](const std::string& p) { this->cmdOTAInfo(p); }, "Show OTA firmware info"};
    commands["otaStatus"] = {"otaStatus", [this](const std::string& p) { this->cmdOTAStatus(p); }, "Show OTA status"};
    commands["help"] = {"help", [this](const std::string& p) { this->cmdHelp(p); }, "Show available commands"};
}

void CommandHandler::cmdIncrementSetpoint(const std::string& params) {
    displayController->incrementSetpoint();
    ESP_LOGI(TAG, "Setpoint incremented.");
}

void CommandHandler::cmdDecrementSetpoint(const std::string& params) {
    displayController->decrementSetpoint();
    ESP_LOGI(TAG, "Setpoint decremented.");
}

void CommandHandler::cmdReset(const std::string& params) {
    ESP_LOGI(TAG, "Restarting ESP...");
    esp_restart();
}

void CommandHandler::cmdSwitchToHome(const std::string& params) {
    displayController->setMode(HOME);
    ESP_LOGI(TAG, "Switched to home page.");
}

void CommandHandler::cmdSwitchToInfo(const std::string& params) {
    displayController->setMode(INFO);
    ESP_LOGI(TAG, "Switched to info page.");
}

void CommandHandler::cmdClearNVS(const std::string& params) {
    nvsManager->clearAll();
    ESP_LOGI(TAG, "NVS memory cleared.");
}

void CommandHandler::cmdConnectWifi(const std::string& params) {
    std::vector<std::string> parts;
    size_t start = 0;
    size_t end = params.find(':');
    while (end != std::string::npos) {
        parts.push_back(params.substr(start, end - start));
        start = end + 1;
        end = params.find(':', start);
    }
    if (start < params.size()) {
        parts.push_back(params.substr(start));
    }

    if (parts.size() < 2) {
        ESP_LOGE(TAG, "[WiFi] Invalid format! Use: connectWifi:SSID:Password[:Slot]");
        return;
    }

    std::string ssid = parts[0];
    std::string password = parts[1];
    int slot = 0;
    if (parts.size() >= 3) {
        try {
            slot = std::stoi(parts[2]);
        } catch (...) {
            ESP_LOGE(TAG, "[WiFi] Invalid slot: %s", parts[2].c_str());
            return;
        }
        if (slot < 0 || slot >= NUM_WIFI_CREDENTIALS) {
            ESP_LOGE(TAG, "[WiFi] Slot out of range: %d", slot);
            return;
        }
    }

    // Save credentials to NVS via NVSManager
    nvsManager->wifiCredentials[slot].ssid = ssid;
    nvsManager->wifiCredentials[slot].password = password;
    nvsManager->wifiCredentials[slot].remember = true;
    nvsManager->lastConnectedNetworkIndex = slot;
    nvsManager->saveWiFiCredentials();
    ESP_LOGI(TAG, "[WiFi] Credentials saved for SSID: %s (slot %d)", ssid.c_str(), slot);

    // Connect to WiFi
    wifiManager->connectToWiFi();
}

void CommandHandler::cmdShowNetworks(const std::string& params) {
    // This needs implementation in WiFiManager to retrieve saved networks from NVS
    ESP_LOGI(TAG, "Showing networks is not implemented yet.");
}

void CommandHandler::cmdCalibrateOrientation(const std::string& params) {
    displayController->setMode(CALIBRATE_ORIENTATION);
    ESP_LOGI(TAG, "Starting orientation calibration...");
}

void CommandHandler::cmdHelp(const std::string& params) {
    printf("\nAvailable Commands:\n");
    for (const auto& cmd_pair : commands) {
        printf("  %s - %s\n", cmd_pair.second.name.c_str(), cmd_pair.second.description.c_str());
    }
}

void CommandHandler::cmdKnobSetpoint(const std::string& params) {
    int setPoint = std::stoi(params);
    displayController->setSetpoint(setPoint);
    ESP_LOGI(TAG, "Updated setpoint: %d", setPoint);
}

void CommandHandler::cmdGetDeviceName(const std::string& params) {
    printf("Device name: %s\n", nvsManager->deviceName.c_str());
}

void CommandHandler::cmdSetDeviceName(const std::string& params) {
    if (params.empty()) {
        ESP_LOGE(TAG, "Device name cannot be empty!");
        return;
    }
    nvsManager->saveDeviceName(params);
    ESP_LOGI(TAG, "Device name set to: %s", params.c_str());
}

void CommandHandler::cmdConfigureMQTTServer(const std::string& params) {
    // Host:Port[:Username:Password]
    auto parsePort = [](const std::string& value, int& portOut) -> bool {
        if (value.empty()) {
            return false;
        }
        char* end = nullptr;
        long parsed = std::strtol(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0' || parsed <= 0 || parsed > 65535) {
            return false;
        }
        portOut = static_cast<int>(parsed);
        return true;
    };

    auto stripScheme = [](const std::string& value) -> std::string {
        if (value.rfind("mqtt://", 0) == 0) {
            return value.substr(7);
        }
        if (value.rfind("mqtts://", 0) == 0) {
            return value.substr(8);
        }
        return value;
    };

    std::vector<std::string> parts;
    size_t start = 0;
    size_t end = params.find(':');
    while (end != std::string::npos) {
        parts.push_back(params.substr(start, end - start));
        start = end + 1;
        end = params.find(':', start);
    }
    parts.push_back(params.substr(start));

    if (parts.size() >= 2 &&
        (parts[0] == "mqtt" || parts[0] == "mqtts") &&
        parts[1].rfind("//", 0) == 0) {
        parts[1] = parts[0] + ":" + parts[1];
        parts.erase(parts.begin());
    }

    std::string host;
    int port = 0;
    std::string username;
    std::string password;

    if (parts.size() == 5 && (parts[0] == "URL" || parts[0] == "url")) {
        host = stripScheme(parts[1]);
        if (!parsePort(parts[2], port)) {
            ESP_LOGE(TAG, "Invalid MQTT port: %s", parts[2].c_str());
            publishResponse("configureMQTTServer:ERR:invalid_port");
            return;
        }
        username = parts[3];
        password = parts[4];
    } else if (parts.size() == 4 && (parts[0] == "URL" || parts[0] == "url")) {
        host = stripScheme(parts[1]);
        username = parts[2];
        password = parts[3];
        port = 8883;
    } else if (parts.size() == 4) {
        host = stripScheme(parts[0]);
        if (!parsePort(parts[1], port)) {
            ESP_LOGE(TAG, "Invalid MQTT port: %s", parts[1].c_str());
            publishResponse("configureMQTTServer:ERR:invalid_port");
            return;
        }
        username = parts[2];
        password = parts[3];
    } else if (parts.size() == 2) {
        host = stripScheme(parts[0]);
        if (!parsePort(parts[1], port)) {
            ESP_LOGE(TAG, "Invalid MQTT port: %s", parts[1].c_str());
            publishResponse("configureMQTTServer:ERR:invalid_port");
            return;
        }
    } else {
        ESP_LOGE(TAG, "Invalid format! Use: configureMQTTServer:Host:Port[:User:Pass]");
        publishResponse("configureMQTTServer:ERR:invalid_format");
        return;
    }

    if (host.empty()) {
        ESP_LOGE(TAG, "MQTT host cannot be empty");
        publishResponse("configureMQTTServer:ERR:empty_host");
        return;
    }

    if (password == "-") {
        password = nvsManager->soundMQTTPassword;
    }

    // Save MQTT config via NVSManager (configures sound MQTT server)
    nvsManager->saveSoundMQTTServer(host, port, username, password);
    ESP_LOGI(TAG, "MQTT Server configured. It will be applied on next restart.");
    publishResponse("configureMQTTServer:OK:restart_required");
}

void CommandHandler::cmdCommInfo(const std::string& params) {
    // This needs to be implemented to get info from MQTTManager, WiFiManager etc.
    ESP_LOGI(TAG, "Communication info display is not implemented yet.");
}

void CommandHandler::cmdConfigureStaticIP(const std::string& params) {
    std::vector<std::string> parts;
    size_t start = 0;
    size_t end = params.find(':');
    while (end != std::string::npos) {
        parts.push_back(params.substr(start, end - start));
        start = end + 1;
        end = params.find(':', start);
    }
    if (start < params.size()) {
        parts.push_back(params.substr(start));
    }

    if (parts.size() < 5) {
        ESP_LOGE(TAG, "Invalid format! Use: configureStaticIP:IP:Gateway:Subnet:DNS1:DNS2");
        return;
    }

    const std::string& ip = parts[0];
    const std::string& gateway = parts[1];
    const std::string& subnet = parts[2];
    const std::string& dns1 = parts[3];
    const std::string& dns2 = parts[4];

    nvsManager->saveStaticIPConfig(true, ip, gateway, subnet, dns1, dns2);
    ESP_LOGI(TAG, "Static IP configuration saved. Restart required.");
}

void CommandHandler::cmdEnableStaticIP(const std::string& params) {
    nvsManager->saveStaticIPConfig(true,
                                   nvsManager->staticIP,
                                   nvsManager->staticGateway,
                                   nvsManager->staticSubnet,
                                   nvsManager->staticDNS1,
                                   nvsManager->staticDNS2);
    ESP_LOGI(TAG, "Static IP enabled. Restart required.");
}

void CommandHandler::cmdDisableStaticIP(const std::string& params) {
    nvsManager->saveStaticIPConfig(false,
                                   nvsManager->staticIP,
                                   nvsManager->staticGateway,
                                   nvsManager->staticSubnet,
                                   nvsManager->staticDNS1,
                                   nvsManager->staticDNS2);
    ESP_LOGI(TAG, "Static IP disabled. Restart required.");
}

void CommandHandler::cmdShowStaticIP(const std::string& params) {
    printf("staticIPEnabled=%s\n", nvsManager->staticIPEnabled ? "true" : "false");
    printf("staticIP=%s\n", nvsManager->staticIP.c_str());
    printf("staticGateway=%s\n", nvsManager->staticGateway.c_str());
    printf("staticSubnet=%s\n", nvsManager->staticSubnet.c_str());
    printf("staticDNS1=%s\n", nvsManager->staticDNS1.c_str());
    printf("staticDNS2=%s\n", nvsManager->staticDNS2.c_str());
}

void CommandHandler::cmdListNVSValues(const std::string& params) {
    if (nvsManager != nullptr) {
        nvsManager->loadAllConfigurations();
    }

    for (int i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        printf("wifi%d_ssid=%s\n", i, nvsManager->wifiCredentials[i].ssid.c_str());
        if (!nvsManager->wifiCredentials[i].password.empty()) {
            printf("wifi%d_password=********\n", i);
        } else {
            printf("wifi%d_password=\n", i);
        }
    }
    printf("lastConnectedNetwork=%d\n", nvsManager->lastConnectedNetworkIndex);
    printf("deviceName=%s\n", nvsManager->deviceName.c_str());

    printf("soundMQTTURL=%s\n", nvsManager->soundMQTTServerURL.c_str());
    printf("soundMQTTPort=%d\n", nvsManager->soundMQTTServerPort);
    printf("soundMQTTUsername=%s\n", nvsManager->soundMQTTUsername.c_str());
    printf("soundMQTTPassword=%s\n", nvsManager->soundMQTTPassword.empty() ? "" : "********");

    printf("lightMQTTURL=%s\n", nvsManager->lightMQTTServerURL.c_str());
    printf("lightMQTTPort=%d\n", nvsManager->lightMQTTServerPort);
    printf("lightMQTTUsername=%s\n", nvsManager->lightMQTTUsername.c_str());
    printf("lightMQTTPassword=%s\n", nvsManager->lightMQTTPassword.empty() ? "" : "********");

    printf("staticIPEnabled=%s\n", nvsManager->staticIPEnabled ? "true" : "false");
    printf("staticIP=%s\n", nvsManager->staticIP.c_str());
    printf("staticGateway=%s\n", nvsManager->staticGateway.c_str());
    printf("staticSubnet=%s\n", nvsManager->staticSubnet.c_str());
    printf("staticDNS1=%s\n", nvsManager->staticDNS1.c_str());
    printf("staticDNS2=%s\n", nvsManager->staticDNS2.c_str());

    printf("End of EEPROM Values\n");
}

void CommandHandler::cmdStartSoundRecord(const std::string& params) {
    if (mediaController == nullptr) {
        ESP_LOGE(TAG, "MediaController not registered.");
        return;
    }

    esp_err_t err = mediaController->startSoundRecord(params);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sound recording: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Sound recording started.");
}

void CommandHandler::cmdStopSoundRecord(const std::string& params) {
    (void)params;
    if (mediaController == nullptr) {
        ESP_LOGE(TAG, "MediaController not registered.");
        return;
    }

    esp_err_t err = mediaController->stopSoundRecord();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop sound recording: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Sound recording stopped.");
}

void CommandHandler::cmdPlayLastSoundRecord(const std::string& params) {
    (void)params;
    if (mediaController == nullptr) {
        ESP_LOGE(TAG, "MediaController not registered.");
        return;
    }

    esp_err_t err = mediaController->playLastSoundRecord();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to play last recording: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Playing last sound recording.");
}

void CommandHandler::cmdOTAUpdate(const std::string& params) {
    if (otaManager == nullptr) {
        ESP_LOGE(TAG, "OTAManager not registered.");
        publishResponse("otaUpdate:ERR:ota_manager_not_registered");
        return;
    }

    std::string url = params;
    url.erase(url.begin(), std::find_if(url.begin(), url.end(), [](unsigned char c) {
        return !std::isspace(c);
    }));
    url.erase(std::find_if(url.rbegin(), url.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(), url.end());

    if (url.empty()) {
        ESP_LOGE(TAG, "Usage: otaUpdate:URL");
        publishResponse("otaUpdate:ERR:missing_url");
        return;
    }

    esp_err_t err = otaManager->startUpdate(url, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start OTA: %s", esp_err_to_name(err));
        publishResponse(std::string("otaUpdate:ERR:") + esp_err_to_name(err));
        return;
    }
    publishResponse("otaUpdate:OK");
}

void CommandHandler::cmdOTAInfo(const std::string& params) {
    (void)params;
    if (otaManager == nullptr) {
        ESP_LOGE(TAG, "OTAManager not registered.");
        publishResponse("otaInfo:ERR:ota_manager_not_registered");
        return;
    }
    const std::string version = otaManager->getRunningVersion();
    const std::string partition = otaManager->getRunningPartitionLabel();
    printf("OTA version: %s\n", version.c_str());
    printf("OTA partition: %s\n", partition.c_str());
    publishResponse("otaInfo:version=" + version + ",partition=" + partition);
}

void CommandHandler::cmdOTAStatus(const std::string& params) {
    (void)params;
    if (otaManager == nullptr) {
        ESP_LOGE(TAG, "OTAManager not registered.");
        publishResponse("otaStatus:ERR:ota_manager_not_registered");
        return;
    }
    const bool busy = otaManager->isBusy();
    const char* err = esp_err_to_name(otaManager->getLastError());
    printf("OTA busy: %s\n", busy ? "yes" : "no");
    printf("OTA last error: %s\n", err);
    publishResponse(std::string("otaStatus:busy=") + (busy ? "yes" : "no") + ",last_error=" + err);
}
