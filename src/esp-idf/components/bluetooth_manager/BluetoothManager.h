#pragma once

#include "esp_err.h"
#include "esp_bt_defs.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct BtScanResult {
    std::string address;
    std::string name;
    int rssi;
};

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
    std::string getConnectedHidAddress() const;
    std::string getConnectedSerialAddress() const;
    esp_err_t disconnectDevice(const std::string& address);

    esp_err_t setEnabled(bool enable);
    esp_err_t restartAdvertising();
    esp_err_t clearBonds();
    int getBondedDeviceCount() const;
    esp_err_t startScan(int durationSec = 5);
    bool isScanning() const { return scanning; }
    std::string getScanStatus() const { return scanStatusMsg; }
    std::vector<BtScanResult> getScanResults() const;
    esp_err_t sendPlayPause();
    void updateBatteryLevel(bool batteryConnected, float percentage);
    esp_err_t sendSerial(const uint8_t* data, size_t length);
    esp_err_t sendSerialLine(const std::string& line);

    // Called with each complete newline-terminated line received on the RX
    // characteristic.  Set once at startup; invoked from the BT event task.
    using SerialLineCallback = std::function<void(const std::string& line)>;
    void setSerialLineCallback(SerialLineCallback cb);

    // Advertising hold — delegates to ConnectivityManager (T-15).
    // Ref-counted: advertising resumes only when all holds are released.
    void requestAdvertisingHold(const char* reason);
    void releaseAdvertisingHold(const char* reason);

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

    SerialLineCallback serial_line_cb_;
    std::string ble_rx_buf_;

    bool enabled = true;
    bool initialized = false;
    bool ready = false;
    bool scanning = false;
    bool scanPending = false;
    int scanDuration = 5;
    std::string scanStatusMsg;
    std::vector<BtScanResult> lastScanResults;
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
    bool advDataConfigured = false;
    bool scanRspConfigured = false;
    bool scanRspConfigPending = false;
    uint16_t serialMtu = 23;
    uint16_t serialHandles[6] = {};
    int serialGattsIf = 0xff;
    uint8_t hidBatteryLevel = 0xff;
    std::string name = "Qnob PC Control";
};
