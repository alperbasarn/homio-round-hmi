#include "CommandHandler.h"
#include "CommandEnvelope.h"
#include "CommandParsers.h"
#include "DisplayController.h"
#include "NVSManager.h"
#include "model/KnobModel.h"
#include "WiFiManager.h"
#include "MQTTManager.h"
#include "LocalAudio.h"
#include "OTAManager.h"
#include "BluetoothManager.h"
#include "adapters/MqttModelBridge.h"
#include "model/LedRegistryModel.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if CONFIG_USJ_ENABLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#endif
#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static const char *TAG = "CommandHandler";

// File descriptor for stdin (console input)
static int stdin_fd = -1;

CommandHandler::CommandHandler(DisplayController* dc, NVSManager* nvs, WiFiManager* wifi, MQTTManager* mqtt)
    : displayController(dc),
      nvsManager(nvs),
      wifiManager(wifi),
      mqttManager(mqtt),
      localAudio(nullptr),
      otaManager(nullptr),
      bluetoothManager(nullptr),
      ledBridge(nullptr),
      modelQueue(nullptr),
      lastSetpointSeen(-1),
      dispatch_mutex_(xSemaphoreCreateMutex()),
      response_capture_(nullptr) {
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

void CommandHandler::registerLocalAudio(LocalAudio* mc) {
    localAudio = mc;
}

void CommandHandler::registerOTAManager(OTAManager* ota) {
    otaManager = ota;
}

void CommandHandler::registerBluetoothManager(BluetoothManager* bt) {
    bluetoothManager = bt;
}

void CommandHandler::registerLedBridge(adapters::MqttModelBridge* bridge) {
    ledBridge = bridge;
}

void CommandHandler::setOtaConfigUpdatedCallback(std::function<void(void)> callback) {
    otaConfigUpdatedCallback = std::move(callback);
}

void CommandHandler::setPairedStateCallback(std::function<void(bool)> callback) {
    pairedStateCallback_ = std::move(callback);
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
                dispatch(command, CommandSource::Serial);
            }
        }
    }

    // APP-04: never let a newline-less stream grow the accumulator without bound.
    if (commandFromPC.size() > kMaxConsoleLineBytes) {
        ESP_LOGW(TAG, "Console line exceeded %u bytes without a newline; dropping %u bytes",
                 static_cast<unsigned>(kMaxConsoleLineBytes), static_cast<unsigned>(commandFromPC.size()));
        commandFromPC.clear();
    }

}

void CommandHandler::handleExternalCommand(const std::string& commandLine, CommandSource source) {
    const std::string command = trimCommand(commandLine);
    if (command.empty()) {
        ESP_LOGW(TAG, "Ignoring empty external command");
        return;
    }
    ESP_LOGI(TAG, "External command: %s", command.c_str());
    dispatch(command, source);
}

void CommandHandler::handleExternalCommand(const std::string& commandLine, std::string& responseOut) {
    const std::string trimmed = trimCommand(commandLine);
    if (trimmed.empty()) {
        ESP_LOGW(TAG, "Ignoring empty external command");
        return;
    }
    ESP_LOGI(TAG, "External command (captured): %.80s", trimmed.c_str());

    // Decode the envelope (JSON or legacy colon-delimited).
    QnobEnvelope env;
    if (!qnob_envelope_decode(trimmed.c_str(), &env)) {
        ESP_LOGW(TAG, "Cannot decode command envelope");
        return;
    }

    // For JSON envelopes only: validate auth before dispatching (main's A6). A line without an
    // envelope carries no token, so it is held to the MQTT allowlist (APP-03).
    CommandSource source = CommandSource::RemoteLine;
    if (!env.is_legacy) {
        const bool skip_auth = (strcmp(env.cmd, "pair") == 0 ||
                                strcmp(env.cmd, "ping") == 0 ||
                                strcmp(env.cmd, "unpair") == 0);
        if (!skip_auth && nvsManager != nullptr && !nvsManager->pairToken.empty()) {
            if (strcmp(env.auth, nvsManager->pairToken.c_str()) != 0) {
                char buf[128];
                qnob_envelope_encode_err(env.id, "unauthorized",
                                         "invalid or missing auth token", buf, sizeof(buf));
                responseOut = buf;
                ESP_LOGW(TAG, "Auth rejected for cmd=%s", env.cmd);
                return;
            }
        }
        source = CommandSource::Remote;
    }

    // Reconstruct legacy-style "cmd[:params]" for processCommand.
    std::string cmd_line = env.cmd;
    const char* params = env.is_legacy ? env.legacy_param : env.params_json;
    if (params[0] != '\0') {
        cmd_line += ':';
        cmd_line += params;
    }

    // Dispatch under mutex, capturing raw response.
    std::string raw;
    if (dispatch_mutex_) xSemaphoreTake(dispatch_mutex_, portMAX_DELAY);
    response_capture_ = &raw;
    processCommand(cmd_line, source);
    response_capture_ = nullptr;
    if (dispatch_mutex_) xSemaphoreGive(dispatch_mutex_);

    // For JSON envelopes: wrap raw response in a proper JSON envelope.
    responseOut = env.is_legacy ? raw : wrapResponse_(env.id, raw);
}

void CommandHandler::dispatch(const std::string& command, CommandSource source) {
    if (dispatch_mutex_) xSemaphoreTake(dispatch_mutex_, portMAX_DELAY);
    processCommand(command, source);
    if (dispatch_mutex_) xSemaphoreGive(dispatch_mutex_);
}

void CommandHandler::processCommand(const std::string& command_line, CommandSource source) {
    static const char* const kSourceNames[] = {"serial", "knob", "mqtt", "portal", "remote", "remote-line"};
    ESP_LOGI(TAG, "Processing command (%s): %s", kSourceNames[static_cast<int>(source)], command_line.c_str());
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
    if (it == commands.end()) {
        ESP_LOGE(TAG, "Unknown command: %s", command.c_str());
        return;
    }
    // APP-03: MQTT is an unauthenticated, shared channel, and so is a remote line without an
    // envelope. Anything that writes secrets, restarts or reflashes the device, or drives the UI
    // into an unhandled state only runs from a physical source, the setup portal, or a paired
    // client.
    const bool restricted = source == CommandSource::Mqtt || source == CommandSource::RemoteLine;
    if (restricted && !it->second.allowedFromMqtt) {
        ESP_LOGW(TAG, "Refusing '%s' from %s: not on the remote allowlist", command.c_str(),
                 source == CommandSource::Mqtt ? "MQTT" : "an unauthenticated remote line");
        publishResponse(command + (source == CommandSource::Mqtt ? ":ERR:refused_from_mqtt" : ":ERR:unauthorized"));
        return;
    }
    it->second.callback(params);
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

bool CommandHandler::parseInt(const std::string& text, int& out, int min, int max) {
    return cmdparse::parseInt(text, out, min, max);  // APP-21: the grammar lives in the host-tested leaf
}

std::string CommandHandler::urlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const char hex[3] = {value[i + 1], value[i + 2], '\0'};
            out.push_back(static_cast<char>(strtol(hex, nullptr, 16)));
            i += 2;
        } else if (value[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::string CommandHandler::getFormValue(const std::string& body, const std::string& key) {
    const std::string needle = key + "=";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) {
        return "";
    }
    pos += needle.size();
    size_t end = body.find('&', pos);
    return urlDecode(body.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
}

// static
std::string CommandHandler::wrapResponse_(uint32_t id, const std::string& raw) {
    char buf[1024];
    // JSON object published by the command → use as data payload.
    if (!raw.empty() && raw[0] == '{') {
        if (qnob_envelope_encode_ok(id, raw.c_str(), buf, sizeof(buf))) return buf;
    }
    // Contains ":ERR:" (legacy error marker) → error envelope.
    const size_t err_pos = raw.find(":ERR:");
    if (err_pos != std::string::npos) {
        const std::string detail = raw.substr(err_pos + 5);
        if (qnob_envelope_encode_err(id, "command_failed", detail.c_str(), buf, sizeof(buf))) return buf;
    }
    // Anything else (empty, ":OK", plain text) → generic ok.
    if (qnob_envelope_encode_ok(id, nullptr, buf, sizeof(buf))) return buf;
    return "";
}

// static
std::string CommandHandler::cmdParam(const std::string& params,
                                     const char* json_key,
                                     unsigned legacy_pos) {
    // Try JSON object first.
    if (!params.empty() && params[0] == '{') {
        cJSON* root = cJSON_Parse(params.c_str());
        if (root) {
            cJSON* item = cJSON_GetObjectItemCaseSensitive(root, json_key);
            std::string result;
            if (cJSON_IsString(item) && item->valuestring) {
                result = item->valuestring;
            } else if (cJSON_IsNumber(item)) {
                char num[32];
                const double d = item->valuedouble;
                if (d == static_cast<double>(static_cast<int64_t>(d))) {
                    snprintf(num, sizeof(num), "%" PRId64, static_cast<int64_t>(d));
                } else {
                    snprintf(num, sizeof(num), "%g", d);
                }
                result = num;
            } else if (cJSON_IsTrue(item)) {
                result = "on";
            } else if (cJSON_IsFalse(item)) {
                result = "off";
            }
            cJSON_Delete(root);
            return result;
        }
    }
    // Legacy: nth colon-delimited field.
    unsigned idx = 0;
    size_t start = 0;
    while (true) {
        const size_t colon = params.find(':', start);
        if (colon == std::string::npos) {
            return (idx == legacy_pos) ? params.substr(start) : "";
        }
        if (idx == legacy_pos) return params.substr(start, colon - start);
        start = colon + 1;
        ++idx;
    }
}

void CommandHandler::publishResponse(const std::string& response) {
    if (response.empty()) {
        return;
    }
    if (response_capture_) {
        *response_capture_ = response;
    }
    if (mqttManager && mqttManager->isConnected()) {
        mqttManager->publish(MQTT_TOPIC_COMMAND_RESPONSE, response);
    }
}

void CommandHandler::registerCommands() {
    commands["reset"] = {"reset", [this](const std::string& p) { this->cmdReset(p); }, "Restart ESP"};
    commands["factoryReset"] = {"factoryReset", [this](const std::string& p) { this->cmdFactoryReset(p); }, "Wipe NVS and restore defaults, then restart"};
    commands["home"] = {"home", [this](const std::string& p) { this->cmdSwitchToHome(p); }, "Switch to home page"};
    commands["info"] = {"info", [this](const std::string& p) { this->cmdSwitchToInfo(p); }, "Switch to info page"};
    commands["screen"] = {"screen", [this](const std::string& p) { this->cmdScreen(p); }, "screen:NAME - Switch to named screen (info, deviceInfo, timer, light, sound, temperature, pc)"};
    commands["clearNVS"] = {"clearNVS", [this](const std::string& p) { this->cmdClearNVS(p); }, "Remove all saved data from NVS"};
    commands["connectWifi"] = {"connectWifi", [this](const std::string& p) { this->cmdConnectWifi(p); }, "connectWifi:SSID:Password - Save and connect to Wi-Fi"};
    commands["showNetworks"] = {"showNetworks", [this](const std::string& p) { this->cmdShowNetworks(p); }, "Show stored Wi-Fi networks"};
    commands["stopPortal"] = {"stopPortal", [this](const std::string& p) { (void)p; if (wifiManager) wifiManager->stopPortal(); }, "Stop captive portal HTTP+DNS (T-19)"};
    commands["startPortal"] = {"startPortal", [this](const std::string& p) { (void)p; if (wifiManager) wifiManager->startPortal(); }, "Start captive portal HTTP+DNS (T-19)"};
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
    commands["setBrightness"] = {"setBrightness", [this](const std::string& p) { this->cmdSetBrightness(p); }, "setBrightness:0-100 - Set screen brightness percentage"};
    commands["setBluetooth"] = {"setBluetooth", [this](const std::string& p) { this->cmdSetBluetooth(p); }, "setBluetooth:on|off - Enable or disable Bluetooth LE"};
    commands["setBluetoothName"] = {"setBluetoothName", [this](const std::string& p) { this->cmdSetBluetoothName(p); }, "setBluetoothName:name - Set Bluetooth device name (takes effect after restart)"};
    commands["restartBtAdvertising"] = {"restartBtAdvertising", [this](const std::string& p) { this->cmdRestartBtAdvertising(p); }, "restartBtAdvertising - Restart BLE advertising so new devices can find this device"};
    commands["clearBtBonds"] = {"clearBtBonds", [this](const std::string& p) { this->cmdClearBtBonds(p); }, "clearBtBonds - Remove all bonded BLE devices so any device can pair"};
    commands["disconnectBtDevice"] = {"disconnectBtDevice", [this](const std::string& p) { this->cmdDisconnectBtDevice(p); }, "disconnectBtDevice:ADDR - Disconnect a currently connected BT device"};
    commands["startBtScan"] = {"startBtScan", [this](const std::string& p) { this->cmdStartBtScan(p); }, "startBtScan[:seconds] - Scan for nearby BLE devices"};
    commands["forgetBtDevice"] = {"forgetBtDevice", [this](const std::string& p) { this->cmdForgetBtDevice(p); }, "forgetBtDevice:ADDR - Remove bonded BT device"};
    commands["otaUpdate"] = {"otaUpdate", [this](const std::string& p) { this->cmdOTAUpdate(p); }, "otaUpdate:URL - Start OTA update"};
    commands["otaInfo"] = {"otaInfo", [this](const std::string& p) { this->cmdOTAInfo(p); }, "Show OTA firmware info"};
    commands["otaStatus"] = {"otaStatus", [this](const std::string& p) { this->cmdOTAStatus(p); }, "Show OTA status"};
    commands["portalDevice"] = {"portalDevice", [this](const std::string& p) { this->cmdPortalDevice(p); }, "Web portal device form"};
    commands["portalWifi"] = {"portalWifi", [this](const std::string& p) { this->cmdPortalWifi(p); }, "Web portal wifi form"};
    commands["portalStaticIpCurrent"] = {"portalStaticIpCurrent", [this](const std::string& p) { this->cmdPortalStaticIpCurrent(p); }, "Web portal static IP from current connection"};
    commands["portalMqtt"] = {"portalMqtt", [this](const std::string& p) { this->cmdPortalMqtt(p); }, "Web portal MQTT form"};
    commands["portalWeather"] = {"portalWeather", [this](const std::string& p) { this->cmdPortalWeather(p); }, "Web portal weather form"};
    commands["portalTime"] = {"portalTime", [this](const std::string& p) { this->cmdPortalTime(p); }, "Web portal time form"};
    commands["portalOta"] = {"portalOta", [this](const std::string& p) { this->cmdPortalOta(p); }, "Web portal OTA form/actions"};
    commands["portalWifiCredential"] = {"portalWifiCredential", [this](const std::string& p) { this->cmdPortalWifiCredential(p); }, "Web portal save wifi credential slot"};
    commands["pair"] = {"pair", [this](const std::string& p) { this->cmdPair(p); }, "Return pairing token (no auth required)"};
    commands["unpair"] = {"unpair", [this](const std::string& p) { this->cmdUnpair(p); }, "Clear pairing state and regenerate token (no auth required)"};
    commands["ping"] = {"ping", [this](const std::string& p) { this->cmdPing(p); }, "Heartbeat - returns uptime_ms (no auth required)"};
    commands["status"] = {"status", [this](const std::string& p) { this->cmdStatus(p); }, "Device status: WiFi, MQTT, BLE, heap, uptime"};
    commands["sysInfo"] = {"sysInfo", [this](const std::string& p) { this->cmdSysInfo(p); }, "Reset reason, uptime, heap, USB host"};
    commands["leds"] = {"leds", [this](const std::string& p) { this->cmdLeds(p); }, "List the LED strips the broker has shown (LED-03)"};
    commands["ledStatus"] = {"ledStatus", [this](const std::string& p) { this->cmdLedStatus(p); }, "LED bridge counters and pairing state"};
    commands["ledSelect"] = {"ledSelect", [this](const std::string& p) { this->cmdLedSelect(p); }, "ledSelect:<slot> - drive this strip from the light model (-1 clears)"};
    commands["ledPair"] = {"ledPair", [this](const std::string& p) { this->cmdLedPair(p); }, "ledPair:<slot> - open the pairing window on a strip"};
    commands["ledCode"] = {"ledCode", [this](const std::string& p) { this->cmdLedCode(p); }, "ledCode:<1-6> - confirm the blink count"};
    commands["ledUnpair"] = {"ledUnpair", [this](const std::string& p) { this->cmdLedUnpair(p); }, "ledUnpair:<slot> - forget a strip on both sides"};
    commands["help"] = {"help", [this](const std::string& p) { this->cmdHelp(p); }, "Show available commands"};

    // APP-03: the remote (MQTT, envelope-less remote line) allowlist. Everything not listed is
    // refused from those sources: reset, factoryReset, clearNVS, connectWifi, configureMQTTServer,
    // the static-IP writers, setDeviceName, otaUpdate, calibrateOrientation, and every command main
    // added (the portal and Bluetooth writers, screen, setBrightness, status, ping, and pair and
    // unpair: pair hands out the token). A paired client reaches those through an envelope.
    static const char* const kMqttAllowed[] = {
        "setpoint", "home", "info", "getDeviceName", "showNetworks", "commInfo", "sysInfo",
        "showStaticIP", "listNVSValues", "startSoundRecord", "stopSoundRecord",
        "playLastSoundRecord", "otaInfo", "otaStatus", "help",
    };
    for (const char* name : kMqttAllowed) {
        auto it = commands.find(name);
        if (it != commands.end()) {
            it->second.allowedFromMqtt = true;
        }
    }
}

void CommandHandler::cmdReset(const std::string& params) {
    ESP_LOGI(TAG, "Restarting ESP...");
    vTaskDelay(pdMS_TO_TICKS(300)); // allow HTTP response to flush before restart
    esp_restart();
}

void CommandHandler::cmdSwitchToHome(const std::string& params) {
    displayController->requestMode(HOME);  // never setMode() here: this runs on the command or MQTT task
    ESP_LOGI(TAG, "Switched to home page.");
}

void CommandHandler::cmdSwitchToInfo(const std::string& params) {
    displayController->requestMode(INFO);
    ESP_LOGI(TAG, "Switched to info page.");
}

// main's screen command, kept on the display task's request path (showNamedScreen only stores
// atomics). The calibration screen has no update case, so it is refused like calibrateOrientation.
void CommandHandler::cmdScreen(const std::string& params) {
    std::string name = cmdParam(params, "name", 0);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (name.rfind("calibrate", 0) == 0) {
        ESP_LOGW(TAG, "screen:%s refused: the calibration screen is not implemented", name.c_str());
        publishResponse("screen:ERR:not_implemented");
        return;
    }
    if (displayController == nullptr || !displayController->showNamedScreen(name)) {
        publishResponse("screen:ERR:unknown_screen");
        return;
    }
    ESP_LOGI(TAG, "Screen switch requested: %s", name.c_str());
    publishResponse("screen:OK:" + name);
}

void CommandHandler::cmdClearNVS(const std::string& params) {
    nvsManager->clearAll();
    ESP_LOGI(TAG, "NVS memory cleared.");
}

void CommandHandler::cmdFactoryReset(const std::string& params) {
    ESP_LOGW(TAG, "Factory reset requested: wiping full NVS partition");

    // Clear app-managed namespace first (best effort) so in-memory state is reset as well.
    if (nvsManager != nullptr) {
        const esp_err_t clearErr = nvsManager->clearAll();
        if (clearErr != ESP_OK) {
            ESP_LOGW(TAG, "Factory reset pre-clear warning: %s", esp_err_to_name(clearErr));
        }
    }

    nvs_flash_deinit();
    const esp_err_t eraseErr = nvs_flash_erase();
    if (eraseErr != ESP_OK) {
        ESP_LOGE(TAG, "Factory reset failed while erasing NVS partition: %s", esp_err_to_name(eraseErr));
        return;
    }

    const esp_err_t initErr = nvs_flash_init();
    if (initErr != ESP_OK) {
        ESP_LOGE(TAG, "Factory reset failed while reinitializing NVS: %s", esp_err_to_name(initErr));
        return;
    }

    ESP_LOGI(TAG, "Factory reset complete. Defaults on reboot: device=Homio-0000, AP open.");
    esp_restart();
}

void CommandHandler::cmdConnectWifi(const std::string& params) {
    cmdparse::WifiCredentials creds;
    std::string error;
    if (!params.empty() && params[0] == '{') {
        // A JSON envelope's params (main's A5): {"ssid":..,"password":..,"slot":..}.
        creds.ssid = cmdParam(params, "ssid");
        creds.password = cmdParam(params, "password");
        const std::string slot = cmdParam(params, "slot");
        if (creds.ssid.empty()) {
            error = "invalid_format";
        } else if (!slot.empty() && !parseInt(slot, creds.slot, 0, NUM_WIFI_CREDENTIALS - 1)) {
            error = "invalid_slot";
        }
    } else {
        cmdparse::parseConnectWifi(params, NUM_WIFI_CREDENTIALS, creds, error);
    }
    if (!error.empty()) {
        ESP_LOGE(TAG, "[WiFi] %s. Use: connectWifi:SSID:Password[:Slot 0-%d]", error.c_str(), NUM_WIFI_CREDENTIALS - 1);
        publishResponse("connectWifi:ERR:" + error);
        return;
    }
    const int slot = creds.slot;

    // Save credentials to NVS via NVSManager
    nvsManager->wifiCredentials[slot].ssid = creds.ssid;
    nvsManager->wifiCredentials[slot].password = creds.password;
    nvsManager->wifiCredentials[slot].remember = true;
    nvsManager->lastConnectedNetworkIndex = slot;
    nvsManager->saveWiFiCredentials();
    ESP_LOGI(TAG, "[WiFi] Credentials saved for SSID: %s (slot %d)", creds.ssid.c_str(), slot);

    // Connect to WiFi
    wifiManager->connectToWiFi();
    publishResponse("connectWifi:OK");
}

void CommandHandler::cmdShowNetworks(const std::string& params) {
    // This needs implementation in WiFiManager to retrieve saved networks from NVS
    ESP_LOGI(TAG, "Showing networks is not implemented yet.");
}

void CommandHandler::cmdCalibrateOrientation(const std::string& params) {
    // CALIBRATE_ORIENTATION has no case in DisplayController::update(), so entering it stopped
    // every screen update and the LVGL pump: a 30 s freeze until the inactivity timeout. Until a
    // calibration screen exists (the mode is deleted for good in Phase 4) this is a logged no-op.
    (void)params;
    ESP_LOGW(TAG, "calibrateOrientation is not implemented; not entering an unhandled screen mode");
    publishResponse("calibrateOrientation:ERR:not_implemented");
}

void CommandHandler::cmdHelp(const std::string& params) {
    printf("\nAvailable Commands:\n");
    for (const auto& cmd_pair : commands) {
        printf("  %s - %s\n", cmd_pair.second.name.c_str(), cmd_pair.second.description.c_str());
    }
}

void CommandHandler::cmdKnobSetpoint(const std::string& params) {
    int setPoint = 0;
    if (!parseInt(params, setPoint, 0, 100)) {
        // APP-01: a bare std::stoi here aborted the chip on any non-numeric payload.
        ESP_LOGW(TAG, "setpoint: rejected '%s' (expected an integer 0-100)", params.c_str());
        publishResponse("setpoint:ERR:invalid_value");
        return;
    }
    // D5 (INPT-22): the screen is authoritative. An absolute value is a delta against the last one
    // seen here, clamped to +-10; the first one only anchors. The model gets the burst record and
    // turns it into its own clamped delta (KnobModel::burstToDelta).
    int delta = 0;
    if (lastSetpointSeen >= 0) {
        delta = setPoint - lastSetpointSeen;
        if (delta > 10) delta = 10;
        if (delta < -10) delta = -10;
    }
    lastSetpointSeen = setPoint;
    if (modelQueue != nullptr) {
        modelQueue->post(model::KnobModel::make(model::KnobModel::Field::Burst, setPoint,
                                                static_cast<uint32_t>(esp_timer_get_time() / 1000)));
    }
    if (delta != 0) {
        displayController->requestKnobDelta(delta);
    }
    ESP_LOGI(TAG, "setpoint %d -> delta %d", setPoint, delta);
}

void CommandHandler::cmdGetDeviceName(const std::string& params) {
    printf("Device name: %s\n", nvsManager->deviceName.c_str());
}

void CommandHandler::cmdSetDeviceName(const std::string& params) {
    // The whole parameter is the name; a JSON envelope sends {"name":..} (main's A5).
    const std::string name = (!params.empty() && params[0] == '{') ? cmdParam(params, "name") : params;
    if (name.empty()) {
        ESP_LOGE(TAG, "Device name cannot be empty!");
        publishResponse("setDeviceName:ERR:empty_name");
        return;
    }
    nvsManager->saveDeviceName(name);
    ESP_LOGI(TAG, "Device name set to: %s", name.c_str());
    publishResponse("setDeviceName:OK");
}

void CommandHandler::cmdConfigureMQTTServer(const std::string& params) {
    // Accept JSON params (main's A5): {"host":"...","port":N,"user":"...","pass":"..."}
    if (!params.empty() && params[0] == '{') {
        const std::string host = cmdParam(params, "host");
        const std::string port_s = cmdParam(params, "port");
        int port = 1883;
        if (host.empty()) {
            publishResponse("configureMQTTServer:ERR:empty_host");
            return;
        }
        if (!port_s.empty() && !parseInt(port_s, port, 1, 65535)) {
            publishResponse("configureMQTTServer:ERR:invalid_port");
            return;
        }
        nvsManager->saveSoundMQTTServer(host, port, cmdParam(params, "user"), cmdParam(params, "pass"));
        publishResponse("configureMQTTServer:OK:restart_required");
        return;
    }

    // Host:Port[:Username:Password] and the URL forms; the grammar is cmdparse (APP-21).
    cmdparse::MqttServerConfig config;
    std::string error;
    if (!cmdparse::parseConfigureMqttServer(params, nvsManager->soundMQTTPassword, config, error)) {
        ESP_LOGE(TAG, "configureMQTTServer rejected (%s). Use: configureMQTTServer:Host:Port[:User:Pass]", error.c_str());
        publishResponse("configureMQTTServer:ERR:" + error);
        return;
    }

    // Save MQTT config via NVSManager (configures sound MQTT server)
    nvsManager->saveSoundMQTTServer(config.host, config.port, config.username, config.password);
    ESP_LOGI(TAG, "MQTT Server configured. It will be applied on next restart.");
    publishResponse("configureMQTTServer:OK:restart_required");
}

void CommandHandler::cmdCommInfo(const std::string& params) {
    // This needs to be implemented to get info from MQTTManager, WiFiManager etc.
    ESP_LOGI(TAG, "Communication info display is not implemented yet.");
}

void CommandHandler::cmdConfigureStaticIP(const std::string& params) {
    cmdparse::StaticIpConfig config;
    std::string error;
    if (!cmdparse::parseStaticIp(params, config, error)) {
        ESP_LOGE(TAG, "Invalid format! Use: configureStaticIP:IP:Gateway:Subnet:DNS1:DNS2");
        return;
    }

    nvsManager->saveStaticIPConfig(true, config.ip, config.gateway, config.subnet, config.dns1, config.dns2);
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
    // APP-24: this used to reload every configuration from NVS first, silently discarding any
    // value set in RAM but not yet committed. It now prints what the running system holds.
    (void)params;
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
    if (localAudio == nullptr) {
        ESP_LOGE(TAG, "LocalAudio not registered.");
        return;
    }

    esp_err_t err = localAudio->startSoundRecord(params);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sound recording: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Sound recording started.");
}

void CommandHandler::cmdStopSoundRecord(const std::string& params) {
    (void)params;
    if (localAudio == nullptr) {
        ESP_LOGE(TAG, "LocalAudio not registered.");
        return;
    }

    esp_err_t err = localAudio->stopSoundRecord();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop sound recording: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Sound recording stopped.");
}

void CommandHandler::cmdPlayLastSoundRecord(const std::string& params) {
    (void)params;
    if (localAudio == nullptr) {
        ESP_LOGE(TAG, "LocalAudio not registered.");
        return;
    }

    esp_err_t err = localAudio->playLastSoundRecord();
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
    if (response_capture_ == nullptr) {
        publishResponse("otaInfo:version=" + version + ",partition=" + partition);  // the PC app parses this over MQTT
        return;
    }
    // A JSON object inside the envelope, so the companion can parse it (main's C3).
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "version", version.c_str());
    cJSON_AddStringToObject(obj, "partition", partition.c_str());
    char* s = cJSON_PrintUnformatted(obj);
    publishResponse(s ? s : "{}");
    cJSON_free(s);
    cJSON_Delete(obj);
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

// Bench diagnostics: why did the board come back, how long has it been up, and is a USB host
// attached (the sleep hold keys off that).
void CommandHandler::cmdSysInfo(const std::string& params) {
    (void)params;
    const char* reason = "unknown";
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: reason = "power-on"; break;
        case ESP_RST_SW: reason = "software (esp_restart)"; break;
        case ESP_RST_PANIC: reason = "panic"; break;
        case ESP_RST_INT_WDT: reason = "interrupt watchdog"; break;
        case ESP_RST_TASK_WDT: reason = "task watchdog"; break;
        case ESP_RST_WDT: reason = "other watchdog"; break;
        case ESP_RST_DEEPSLEEP: reason = "deep-sleep wake"; break;
        case ESP_RST_BROWNOUT: reason = "brownout"; break;
        case ESP_RST_SDIO: reason = "sdio"; break;
        case ESP_RST_USB: reason = "usb"; break;
        case ESP_RST_JTAG: reason = "jtag"; break;
        case ESP_RST_EFUSE: reason = "efuse"; break;
        case ESP_RST_PWR_GLITCH: reason = "power glitch"; break;
        case ESP_RST_CPU_LOCKUP: reason = "cpu lockup"; break;
        default: break;
    }
    const int64_t upMs = esp_timer_get_time() / 1000;
    bool usbHost = false;
#if CONFIG_USJ_ENABLE_USB_SERIAL_JTAG
    usbHost = usb_serial_jtag_is_connected();
#endif
    printf("sysInfo: reset=%s uptime=%llds free_heap=%u min_free_heap=%u usb_host=%s\n", reason,
           static_cast<long long>(upMs / 1000), static_cast<unsigned>(esp_get_free_heap_size()),
           static_cast<unsigned>(esp_get_minimum_free_heap_size()), usbHost ? "attached" : "absent");
    if (diagnosticsProvider) {
        diagnosticsProvider();
    }
}

// ---- LED bench console (LED-03): the LIGHT screen on the registry (LED-05) is the real UI ----

void CommandHandler::cmdLeds(const std::string& params) {
    (void)params;
    if (ledBridge == nullptr) {
        printf("leds: no bridge\n");
        return;
    }
    char lines[640];
    if (ledBridge->describeRegistry(lines, sizeof(lines)) > 0) {
        printf("%s", lines);
    }
}

void CommandHandler::cmdLedStatus(const std::string& params) {
    (void)params;
    if (ledBridge == nullptr) {
        printf("ledStatus: no bridge\n");
        return;
    }
    char line[224];
    if (ledBridge->describeStatus(line, sizeof(line)) > 0) {
        printf("%s\n", line);
    }
}

void CommandHandler::cmdLedSelect(const std::string& params) {
    int slot = 0;
    if (modelQueue == nullptr || !cmdparse::parseInt(params, slot, -1, 7)) {
        printf("ledSelect:ERR:expected a slot -1..7\n");
        return;
    }
    modelQueue->post(model::LedRegistryModel::make(model::LedRegistryModel::Kind::Select, 0, slot,
                                                   static_cast<uint32_t>(esp_timer_get_time() / 1000)));
    printf("ledSelect:%d\n", slot);
}

void CommandHandler::cmdLedPair(const std::string& params) {
    int slot = 0;
    if (modelQueue == nullptr || !cmdparse::parseInt(params, slot, 0, 7)) {
        printf("ledPair:ERR:expected a slot 0..7\n");
        return;
    }
    modelQueue->post(model::LedRegistryModel::make(model::LedRegistryModel::Kind::PairIntent, 0, slot,
                                                   static_cast<uint32_t>(esp_timer_get_time() / 1000)));
    printf("ledPair:%d (watch the strip, then ledCode:<blinks>)\n", slot);
}

void CommandHandler::cmdLedCode(const std::string& params) {
    int code = 0;
    if (modelQueue == nullptr || !cmdparse::parseInt(params, code, 1, 6)) {
        printf("ledCode:ERR:expected 1..6\n");
        return;
    }
    modelQueue->post(model::LedRegistryModel::make(model::LedRegistryModel::Kind::CodeIntent, 0, code,
                                                   static_cast<uint32_t>(esp_timer_get_time() / 1000)));
    printf("ledCode:%d\n", code);
}

void CommandHandler::cmdLedUnpair(const std::string& params) {
    int slot = 0;
    if (modelQueue == nullptr || !cmdparse::parseInt(params, slot, 0, 7)) {
        printf("ledUnpair:ERR:expected a slot 0..7\n");
        return;
    }
    modelQueue->post(model::LedRegistryModel::make(model::LedRegistryModel::Kind::UnpairIntent, 0, slot,
                                                   static_cast<uint32_t>(esp_timer_get_time() / 1000)));
    printf("ledUnpair:%d\n", slot);
}

// ---- Bluetooth, portal and OTA-release commands (main) ----

void CommandHandler::cmdSetBrightness(const std::string& params) {
    if (displayController == nullptr) {
        ESP_LOGE(TAG, "DisplayController not registered.");
        publishResponse("setBrightness:ERR:display_controller_not_registered");
        return;
    }

    const std::string value = cmdParam(params, "brightness", 0);
    if (value.empty()) {
        ESP_LOGE(TAG, "Usage: setBrightness:0-100");
        publishResponse("setBrightness:ERR:missing_percentage");
        return;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        ESP_LOGE(TAG, "Invalid brightness percentage: %s", value.c_str());
        publishResponse("setBrightness:ERR:invalid_percentage");
        return;
    }

    const int percent = std::clamp(static_cast<int>(parsed), 0, 100);
    displayController->requestBrightnessPercent(percent);  // applied on the display task
    publishResponse("setBrightness:OK:" + std::to_string(percent));
}

void CommandHandler::cmdSetBluetooth(const std::string& params) {
    if (bluetoothManager == nullptr) {
        ESP_LOGE(TAG, "BluetoothManager not registered.");
        publishResponse("setBluetooth:ERR:bluetooth_manager_not_registered");
        return;
    }

    std::string value = cmdParam(params, "enabled", 0);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    bool enable = true;
    if (value == "on" || value == "enable" || value == "enabled" || value == "1" || value == "true") {
        enable = true;
    } else if (value == "off" || value == "disable" || value == "disabled" || value == "0" || value == "false") {
        enable = false;
    } else {
        ESP_LOGE(TAG, "Usage: setBluetooth:on|off");
        publishResponse("setBluetooth:ERR:invalid_value");
        return;
    }

    const esp_err_t err = bluetoothManager->setEnabled(enable);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Bluetooth state: %s", esp_err_to_name(err));
        publishResponse("setBluetooth:ERR:" + std::string(esp_err_to_name(err)));
        return;
    }

    // Persist so the setting survives reboot (T-17).
    if (nvsManager != nullptr) {
        nvsManager->bluetoothEnabled = enable;
        nvsManager->saveEnabledFlags();
    }

    publishResponse(std::string("setBluetooth:OK:") + (enable ? "on" : "off"));
}

void CommandHandler::cmdStartBtScan(const std::string& params) {
    if (bluetoothManager == nullptr) {
        publishResponse("startBtScan:ERR:bt_not_registered");
        return;
    }
    int duration = 5;
    const std::string dur_s = cmdParam(params, "duration", 0);
    if (!dur_s.empty()) {
        const int parsed = std::atoi(dur_s.c_str());
        if (parsed > 0 && parsed <= 30) duration = parsed;
    }
    const esp_err_t err = bluetoothManager->startScan(duration);
    if (err != ESP_OK) {
        publishResponse("startBtScan:ERR:" + std::string(esp_err_to_name(err)));
        return;
    }
    publishResponse("startBtScan:OK:duration=" + std::to_string(duration));
}

void CommandHandler::cmdRestartBtAdvertising(const std::string& params) {
    if (bluetoothManager == nullptr) {
        ESP_LOGE(TAG, "BluetoothManager not registered.");
        publishResponse("restartBtAdvertising:ERR:bt_not_registered");
        return;
    }
    const esp_err_t err = bluetoothManager->restartAdvertising();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Restart advertising failed: %s", esp_err_to_name(err));
        publishResponse("restartBtAdvertising:ERR:" + std::string(esp_err_to_name(err)));
        return;
    }
    publishResponse("restartBtAdvertising:OK");
}

void CommandHandler::cmdClearBtBonds(const std::string& params) {
    if (bluetoothManager == nullptr) {
        ESP_LOGE(TAG, "BluetoothManager not registered.");
        publishResponse("clearBtBonds:ERR:bt_not_registered");
        return;
    }
    const int before = bluetoothManager->getBondedDeviceCount();
    const esp_err_t err = bluetoothManager->clearBonds();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Clear bonds failed: %s", esp_err_to_name(err));
        publishResponse("clearBtBonds:ERR:" + std::string(esp_err_to_name(err)));
        return;
    }
    bluetoothManager->restartAdvertising();
    publishResponse("clearBtBonds:OK:cleared=" + std::to_string(before));
}

void CommandHandler::cmdDisconnectBtDevice(const std::string& params) {
    if (bluetoothManager == nullptr) {
        publishResponse("disconnectBtDevice:ERR:bt_not_registered");
        return;
    }
    const std::string addr = params;
    if (addr.empty()) {
        publishResponse("disconnectBtDevice:ERR:missing_address");
        return;
    }
    const esp_err_t ret = bluetoothManager->disconnectDevice(addr);
    if (ret != ESP_OK) {
        publishResponse("disconnectBtDevice:ERR:not_connected");
        return;
    }
    publishResponse("disconnectBtDevice:OK:" + addr);
}

void CommandHandler::cmdSetBluetoothName(const std::string& params) {
    const std::string name = cmdParam(params, "name", 0);
    if (name.empty()) {
        ESP_LOGE(TAG, "Usage: setBluetoothName:name");
        publishResponse("setBluetoothName:ERR:empty_name");
        return;
    }
    if (nvsManager == nullptr) {
        ESP_LOGE(TAG, "NVSManager not available");
        publishResponse("setBluetoothName:ERR:nvs_unavailable");
        return;
    }
    nvsManager->saveBluetoothName(name);
    publishResponse("setBluetoothName:OK:restart_required");
}

void CommandHandler::cmdForgetBtDevice(const std::string& params) {
    if (nvsManager == nullptr) {
        ESP_LOGE(TAG, "forgetBtDevice unavailable");
        return;
    }
    // params is the MAC address directly
    const std::string addr = params.empty() ? getFormValue(params, "address") : params;
    if (addr.empty()) {
        ESP_LOGW(TAG, "forgetBtDevice missing address");
        return;
    }
    esp_err_t ret = nvsManager->removeBondedDevice(addr);
    ESP_LOGI(TAG, "forgetBtDevice %s: %s", addr.c_str(), esp_err_to_name(ret));
}

void CommandHandler::cmdPortalDevice(const std::string& params) {
    if (nvsManager == nullptr || wifiManager == nullptr) {
        ESP_LOGE(TAG, "portalDevice unavailable");
        return;
    }

    const std::string suffix = getFormValue(params, "device_suffix");
    const std::string password = getFormValue(params, "ap_password");
    const std::string pwEnabled = getFormValue(params, "ap_password_enabled");

    nvsManager->saveDeviceName(suffix);

    if (pwEnabled == "off") {
        // Explicitly disable password → open AP
        nvsManager->saveAccessPointPassword("");
    } else if (pwEnabled == "on" && !password.empty()) {
        esp_err_t passwordErr = nvsManager->saveAccessPointPassword(password);
        if (passwordErr != ESP_OK) {
            ESP_LOGW(TAG, "portalDevice AP password rejected: %s", esp_err_to_name(passwordErr));
            return;
        }
    } else if (pwEnabled.empty() && !password.empty()) {
        // Legacy / backward compat: no toggle sent, just a password
        nvsManager->saveAccessPointPassword(password);
    }

    esp_err_t wifiErr = wifiManager->syncAccessPointConfig();
    if (wifiErr != ESP_OK) {
        ESP_LOGW(TAG, "portalDevice AP refresh failed: %s", esp_err_to_name(wifiErr));
    }
}

void CommandHandler::cmdPortalWifi(const std::string& params) {
    if (wifiManager == nullptr) {
        ESP_LOGE(TAG, "portalWifi unavailable");
        return;
    }

    const std::string ssid = getFormValue(params, "ssid");
    const std::string password = getFormValue(params, "password");
    if (ssid.empty()) {
        ESP_LOGW(TAG, "portalWifi missing SSID");
        return;
    }

    esp_err_t err = wifiManager->connectToNetwork(ssid, password, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "portalWifi connect failed: %s", esp_err_to_name(err));
    }
}

void CommandHandler::cmdPortalStaticIpCurrent(const std::string& params) {
    (void)params;
    if (wifiManager == nullptr) {
        ESP_LOGE(TAG, "portalStaticIpCurrent unavailable");
        return;
    }

    esp_err_t err = wifiManager->saveCurrentConnectionAsStaticIP();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "portalStaticIpCurrent failed: %s", esp_err_to_name(err));
    }
}

void CommandHandler::cmdPortalMqtt(const std::string& params) {
    if (nvsManager == nullptr) {
        ESP_LOGE(TAG, "portalMqtt unavailable");
        return;
    }

    const std::string url = getFormValue(params, "url");
    const std::string portStr = getFormValue(params, "port");
    const std::string username = getFormValue(params, "username");
    const std::string password = getFormValue(params, "password");

    int port = 8883;
    if (!portStr.empty()) {
        port = std::max(1, std::min(65535, atoi(portStr.c_str())));
    }

    nvsManager->saveSoundMQTTServer(url, port, username, password);
    nvsManager->saveLightMQTTServer(url, port, username, password);
}

void CommandHandler::cmdPortalWeather(const std::string& params) {
    if (nvsManager == nullptr) {
        ESP_LOGE(TAG, "portalWeather unavailable");
        return;
    }

    const std::string token = getFormValue(params, "api_token");
    const std::string city = getFormValue(params, "city");
    const std::string country = getFormValue(params, "country");
    nvsManager->saveWeatherConfig(token, city.empty() ? "Istanbul" : city, country.empty() ? "tr" : country);
}

void CommandHandler::cmdPortalTime(const std::string& params) {
    if (nvsManager == nullptr) {
        ESP_LOGE(TAG, "portalTime unavailable");
        return;
    }

    const std::string token = getFormValue(params, "api_token");
    nvsManager->saveTimeApiToken(token);
}

void CommandHandler::cmdPortalWifiCredential(const std::string& params) {
    if (nvsManager == nullptr || wifiManager == nullptr) {
        ESP_LOGE(TAG, "portalWifiCredential unavailable");
        return;
    }

    const std::string indexStr = getFormValue(params, "index");
    const std::string ssid = getFormValue(params, "ssid");
    const std::string password = getFormValue(params, "password");
    const std::string staticIp = getFormValue(params, "static_ip");

    char* end = nullptr;
    int idx = static_cast<int>(strtol(indexStr.c_str(), &end, 10));
    if (end == indexStr.c_str() || idx < 0 || idx >= NUM_WIFI_CREDENTIALS) {
        ESP_LOGW(TAG, "portalWifiCredential invalid index: %s", indexStr.c_str());
        return;
    }

    nvsManager->wifiCredentials[idx].ssid = ssid;
    nvsManager->wifiCredentials[idx].password = password;
    nvsManager->wifiCredentials[idx].remember = !ssid.empty();
    nvsManager->wifiCredentials[idx].static_ip = staticIp;
    nvsManager->saveWiFiCredentials();
    ESP_LOGI(TAG, "portalWifiCredential slot %d saved: ssid=%s", idx, ssid.c_str());

    // If saving to current active slot or connecting a new SSID, trigger connect
    if (!ssid.empty()) {
        wifiManager->connectToNetwork(ssid, password, true);
    }
}

void CommandHandler::cmdPortalOta(const std::string& params) {
    if (nvsManager == nullptr) {
        ESP_LOGE(TAG, "portalOta unavailable");
        return;
    }

    const std::string action = getFormValue(params, "action");
    if (!action.empty()) {
        if (otaManager == nullptr) {
            ESP_LOGE(TAG, "portalOta action unavailable (otaManager null)");
            return;
        }
        if (action == "check") {
            otaManager->checkForReleaseUpdate();
            return;
        }
        if (action == "update") {
            otaManager->startReleaseUpdate(true);
            return;
        }
        ESP_LOGW(TAG, "portalOta unknown action: %s", action.c_str());
        return;
    }

    std::string variant = getFormValue(params, "variant");
    std::string manifestUrl = getFormValue(params, "manifest_url");
    variant.erase(std::remove_if(variant.begin(), variant.end(), [](char c) {
        return !(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-');
    }), variant.end());

    if (variant.empty()) {
        ESP_LOGW(TAG, "portalOta variant is required");
        return;
    }

    nvsManager->saveOtaConfig(variant, manifestUrl);
    if (otaConfigUpdatedCallback) {
        otaConfigUpdatedCallback();
    }
}

// ---------------------------------------------------------------------------
// A6: pair / ping / status
// ---------------------------------------------------------------------------

void CommandHandler::cmdPair(const std::string& params) {
    (void)params;
    if (nvsManager == nullptr || nvsManager->pairToken.empty()) {
        publishResponse("pair:ERR:token_not_ready");
        return;
    }
    if (!nvsManager->isPaired) {
        nvsManager->isPaired = true;
        nvsManager->savePairingState();
        if (pairedStateCallback_) pairedStateCallback_(true);
    }
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "token", nvsManager->pairToken.c_str());
    char* s = cJSON_PrintUnformatted(obj);
    publishResponse(s ? s : "{}");
    cJSON_free(s);
    cJSON_Delete(obj);
}

void CommandHandler::cmdUnpair(const std::string& params) {
    (void)params;
    if (nvsManager == nullptr) {
        publishResponse("unpair:ERR:no_nvs");
        return;
    }
    nvsManager->isPaired = false;
    nvsManager->savePairingState();
    // Regenerate the token so the old one is no longer usable.
    nvsManager->generateAndSavePairToken();
    if (pairedStateCallback_) pairedStateCallback_(false);
    publishResponse("unpair:OK");
    ESP_LOGI(TAG, "Device unpaired via unpair command.");
}

void CommandHandler::cmdPing(const std::string& params) {
    (void)params;
    const uint32_t uptime_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "uptime_ms", uptime_ms);

    // Include WiFi STA MAC so the companion can identify the device over serial.
    uint8_t mac_bytes[6] = {};
    esp_read_mac(mac_bytes, ESP_MAC_WIFI_STA);
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
             mac_bytes[0], mac_bytes[1], mac_bytes[2], mac_bytes[3],
             mac_bytes[4], mac_bytes[5]);
    cJSON_AddStringToObject(obj, "mac", mac_str);

    if (nvsManager != nullptr && !nvsManager->deviceName.empty()) {
        cJSON_AddStringToObject(obj, "name", nvsManager->deviceName.c_str());
    }

    char* s = cJSON_PrintUnformatted(obj);
    publishResponse(s ? s : "{}");
    cJSON_free(s);
    cJSON_Delete(obj);
}

void CommandHandler::cmdStatus(const std::string& params) {
    (void)params;
    const uint32_t uptime_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    const uint32_t free_heap = esp_get_free_heap_size();

    cJSON* obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "uptime_ms", uptime_ms);
    cJSON_AddNumberToObject(obj, "free_heap", free_heap);

    // Wi-Fi
    if (wifiManager != nullptr && wifiManager->isConnected()) {
        cJSON_AddStringToObject(obj, "wifi_ssid", wifiManager->getSSID().c_str());
        cJSON_AddNumberToObject(obj, "wifi_rssi", wifiManager->getSignalStrength());
        cJSON_AddStringToObject(obj, "ip", wifiManager->getIPAddress().c_str());
    } else {
        cJSON_AddStringToObject(obj, "wifi_ssid", "");
        cJSON_AddNullToObject(obj, "wifi_rssi");
        cJSON_AddStringToObject(obj, "ip", "");
    }

    // MQTT
    const bool mqtt_ok = (mqttManager != nullptr && mqttManager->isConnected());
    cJSON_AddBoolToObject(obj, "mqtt_connected", mqtt_ok);

    // BLE serial centrals
    cJSON* centrals = cJSON_CreateArray();
    if (bluetoothManager != nullptr && bluetoothManager->isSerialConnected()) {
        const std::string addr = bluetoothManager->getConnectedSerialAddress();
        if (!addr.empty()) {
            cJSON_AddItemToArray(centrals, cJSON_CreateString(addr.c_str()));
        }
    }
    cJSON_AddItemToObject(obj, "ble_centrals", centrals);

    char* s = cJSON_PrintUnformatted(obj);
    publishResponse(s ? s : "{}");
    cJSON_free(s);
    cJSON_Delete(obj);
}
