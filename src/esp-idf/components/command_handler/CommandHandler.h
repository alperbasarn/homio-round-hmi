#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <string>
#include <vector>
#include <map>
#include "model/MutationQueue.h"
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Forward declarations
class DisplayController;
class NVSManager;
class WiFiManager;
class MQTTManager;
class LocalAudio;
class OTAManager;
class BluetoothManager;
namespace adapters { class MqttModelBridge; }

// Where a command line came from (APP-03). Serial and the knob link are physical and trusted. The
// setup portal sends only its own forms and is trusted for them, as it was on main. Remote is a
// paired client: a JSON envelope over BLE (and TCP once a command server lands) that carries the
// pairing token (main's A6). MQTT is an unauthenticated shared channel, and so is RemoteLine, a
// remote line without an envelope: both only run allowlisted commands.
enum class CommandSource { Serial, Knob, Mqtt, Portal, Remote, RemoteLine };

class CommandHandler {
public:
    CommandHandler(DisplayController* dc, NVSManager* nvs, WiFiManager* wifi, MQTTManager* mqtt);
    void begin();
    void update();
    // A line from MQTT (the default) or from the setup portal.
    void handleExternalCommand(const std::string& commandLine, CommandSource source = CommandSource::Mqtt);
    // A line from a remote transport (BLE serial): JSON envelopes are checked against the pairing
    // token and answered in an envelope; the response is captured into responseOut (main's A4/A6).
    void handleExternalCommand(const std::string& commandLine, std::string& responseOut);
    // INPT-22: setpoint commands become knob bursts for the model and a D5 delta for the screen.
    void setModelQueue(model::MutationQueue* queue) { modelQueue = queue; }
    void registerLocalAudio(LocalAudio* mc);
    void registerOTAManager(OTAManager* ota);
    void registerBluetoothManager(BluetoothManager* bt);
    void registerLedBridge(adapters::MqttModelBridge* bridge);  // LED-03 bench console
    void setOtaConfigUpdatedCallback(std::function<void(void)> callback);
    // Called with true when a pair command succeeds; use to update mDNS (A7).
    void setPairedStateCallback(std::function<void(bool)> callback);
    // Bench diagnostics: extra lines appended to sysInfo by the composition (black box, PMU).
    using DiagnosticsProvider = std::function<void()>;
    void registerDiagnosticsProvider(DiagnosticsProvider provider) { diagnosticsProvider = provider; }

private:
    using CommandFunction = std::function<void(const std::string& params)>;

    struct Command {
        std::string name;
        CommandFunction callback;
        std::string description;
        bool allowedFromMqtt = false;  // APP-03: set for the remote allowlist in registerCommands()
    };

    DisplayController* displayController;
    NVSManager* nvsManager;
    WiFiManager* wifiManager;
    MQTTManager* mqttManager;
    LocalAudio* localAudio;
    OTAManager* otaManager;
    BluetoothManager* bluetoothManager;
    adapters::MqttModelBridge* ledBridge;
    DiagnosticsProvider diagnosticsProvider;
    std::function<void(void)> otaConfigUpdatedCallback;
    std::function<void(bool)> pairedStateCallback_;
    model::MutationQueue* modelQueue;
    int lastSetpointSeen;  // D5: an absolute setpoint is only ever a delta against the last one

    std::map<std::string, Command> commands;
    std::string commandFromPC;
    static constexpr size_t kMaxConsoleLineBytes = 512;  // APP-04: bound on the console accumulator
    // Serial, MQTT, the portal and BLE arrive on four tasks; one command runs at a time.
    SemaphoreHandle_t dispatch_mutex_;
    std::string* response_capture_;

    void registerCommands();
    void dispatch(const std::string& command, CommandSource source);
    void processCommand(const std::string& command, CommandSource source);
    static std::string trimCommand(const std::string& command);
    // APP-01: the one numeric parser. The whole string must be a decimal integer within [min, max].
    static bool parseInt(const std::string& text, int& out, int min, int max);
    static std::string urlDecode(const std::string& value);
    static std::string getFormValue(const std::string& body, const std::string& key);
    void publishResponse(const std::string& response);
    // Wrap a raw command-response string in a JSON envelope with the given id.
    static std::string wrapResponse_(uint32_t id, const std::string& raw);
    // Extract a named value from either JSON params or nth colon-delimited legacy field.
    static std::string cmdParam(const std::string& params, const char* json_key, unsigned legacy_pos = 0);

    // Command handlers
    void cmdReset(const std::string& params);
    void cmdSwitchToHome(const std::string& params);
    void cmdSwitchToInfo(const std::string& params);
    void cmdScreen(const std::string& params);
    void cmdClearNVS(const std::string& params);
    void cmdFactoryReset(const std::string& params);
    void cmdConnectWifi(const std::string& params);
    void cmdShowNetworks(const std::string& params);
    void cmdCalibrateOrientation(const std::string& params);
    void cmdHelp(const std::string& params);
    void cmdKnobSetpoint(const std::string& params);
    void cmdGetDeviceName(const std::string& params);
    void cmdSetDeviceName(const std::string& params);
    void cmdConfigureMQTTServer(const std::string& params);
    void cmdCommInfo(const std::string& params);
    void cmdSysInfo(const std::string& params);
    void cmdLeds(const std::string& params);
    void cmdLedStatus(const std::string& params);
    void cmdLedSelect(const std::string& params);
    void cmdLedPair(const std::string& params);
    void cmdLedCode(const std::string& params);
    void cmdLedUnpair(const std::string& params);
    void cmdConfigureStaticIP(const std::string& params);
    void cmdEnableStaticIP(const std::string& params);
    void cmdDisableStaticIP(const std::string& params);
    void cmdShowStaticIP(const std::string& params);
    void cmdListNVSValues(const std::string& params);
    void cmdStartSoundRecord(const std::string& params);
    void cmdStopSoundRecord(const std::string& params);
    void cmdPlayLastSoundRecord(const std::string& params);
    void cmdSetBrightness(const std::string& params);
    void cmdSetBluetooth(const std::string& params);
    void cmdSetBluetoothName(const std::string& params);
    void cmdRestartBtAdvertising(const std::string& params);
    void cmdClearBtBonds(const std::string& params);
    void cmdDisconnectBtDevice(const std::string& params);
    void cmdStartBtScan(const std::string& params);
    void cmdForgetBtDevice(const std::string& params);
    void cmdOTAUpdate(const std::string& params);
    void cmdOTAInfo(const std::string& params);
    void cmdOTAStatus(const std::string& params);
    void cmdPortalDevice(const std::string& params);
    void cmdPortalWifi(const std::string& params);
    void cmdPortalStaticIpCurrent(const std::string& params);
    void cmdPortalMqtt(const std::string& params);
    void cmdPortalWeather(const std::string& params);
    void cmdPortalTime(const std::string& params);
    void cmdPortalOta(const std::string& params);
    void cmdPortalWifiCredential(const std::string& params);
    void cmdPair(const std::string& params);
    void cmdUnpair(const std::string& params);
    void cmdPing(const std::string& params);
    void cmdStatus(const std::string& params);
};

#endif // COMMAND_HANDLER_H
