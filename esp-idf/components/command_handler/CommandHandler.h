#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <string>
#include <vector>
#include <map>
#include <functional>

// Forward declarations
class DisplayController;
class NVSManager;
class KnobController;
class WiFiManager;
class MQTTManager;
class MediaController;
class OTAManager;
class BluetoothManager;

class CommandHandler {
public:
    CommandHandler(DisplayController* dc, NVSManager* nvs, WiFiManager* wifi, MQTTManager* mqtt);
    void begin();
    void update();
    void handleExternalCommand(const std::string& commandLine);
    void registerKnobController(KnobController* kc);
    void registerMediaController(MediaController* mc);
    void registerOTAManager(OTAManager* ota);
    void registerBluetoothManager(BluetoothManager* bt);
    void setOtaConfigUpdatedCallback(std::function<void(void)> callback);

private:
    using CommandFunction = std::function<void(const std::string& params)>;

    struct Command {
        std::string name;
        CommandFunction callback;
        std::string description;
    };

    DisplayController* displayController;
    NVSManager* nvsManager;
    KnobController* knobController;
    WiFiManager* wifiManager;
    MQTTManager* mqttManager;
    MediaController* mediaController;
    OTAManager* otaManager;
    BluetoothManager* bluetoothManager;
    std::function<void(void)> otaConfigUpdatedCallback;

    std::map<std::string, Command> commands;
    std::string commandFromPC;

    void registerCommands();
    void processCommand(const std::string& command);
    static std::string trimCommand(const std::string& command);
    static std::string urlDecode(const std::string& value);
    static std::string getFormValue(const std::string& body, const std::string& key);
    void publishResponse(const std::string& response);

    // Command handlers
    void cmdIncrementSetpoint(const std::string& params);
    void cmdDecrementSetpoint(const std::string& params);
    void cmdReset(const std::string& params);
    void cmdSwitchToHome(const std::string& params);
    void cmdSwitchToInfo(const std::string& params);
    void cmdClearNVS(const std::string& params);
    void cmdConnectWifi(const std::string& params);
    void cmdShowNetworks(const std::string& params);
    void cmdCalibrateOrientation(const std::string& params);
    void cmdHelp(const std::string& params);
    void cmdKnobSetpoint(const std::string& params);
    void cmdGetDeviceName(const std::string& params);
    void cmdSetDeviceName(const std::string& params);
    void cmdConfigureMQTTServer(const std::string& params);
    void cmdCommInfo(const std::string& params);
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
    void cmdStartBtScan(const std::string& params);
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
};

#endif // COMMAND_HANDLER_H
