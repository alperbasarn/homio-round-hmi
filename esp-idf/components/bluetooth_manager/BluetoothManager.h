#pragma once

#include "esp_err.h"
#include "esp_bt_defs.h"
#include <cstddef>
#include <cstdint>
#include <string>

class BluetoothManager {
public:
    static BluetoothManager& instance();

    esp_err_t begin(const char* deviceName = "Qnob PC Control");

    bool isEnabled() const { return enabled; }
    bool isReady() const { return ready; }
    std::string getName() const { return name; }
    bool isConnected() const { return hidConnected || serialConnected; }
    bool isHidConnected() const { return hidConnected; }
    bool isSerialConnected() const { return serialConnected; }

    esp_err_t setEnabled(bool enable);
    esp_err_t sendPlayPause();
    esp_err_t sendSerial(const uint8_t* data, size_t length);
    esp_err_t sendSerialLine(const std::string& line);

    void onHidEvent(int event, void* param);
    void onGapEvent(int event, void* param);
    void onSerialGattEvent(int event, int gattsIf, void* param);

private:
    BluetoothManager() = default;

    esp_err_t initializeController();
    esp_err_t registerProfiles();
    void configureSecurity();
    void configureAdvertising();
    void startAdvertising();
    void stopAdvertising();
    void registerSerialProfile();
    void disconnectKnownPeers();

    bool enabled = true;
    bool initialized = false;
    bool ready = false;
    bool hidConnected = false;
    bool hidSecure = false;
    bool hidRemoteKnown = false;
    esp_bd_addr_t hidRemoteBda = {0};
    bool serialConnected = false;
    bool serialNotifyEnabled = false;
    bool serialRemoteKnown = false;
    esp_bd_addr_t serialRemoteBda = {0};
    uint16_t hidConnId = 0xffff;
    uint16_t serialConnId = 0xffff;
    uint16_t serialMtu = 23;
    uint16_t serialHandles[6] = {};
    int serialGattsIf = 0xff;
    std::string name = "Qnob PC Control";
};
