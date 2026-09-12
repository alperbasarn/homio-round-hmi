#pragma once
// ConnectivityModel (MDL-22): the four link flags, independent of each other, plus signal bars.
// The RSSI-to-bars rule is model::signalBarsFromRssi, so the poller posts either the raw RSSI
// or an already-mapped strength; both land here.
#include <cstdint>

#include "model/Bands.h"
#include "model/ModelBase.h"
#include "model/ModelTypes.h"
#include "model/MutationQueue.h"

namespace model {

struct ConnectivityState {
    bool wifiConnected = false;
    bool internetAvailable = false;
    bool mqttConnected = false;
    bool mqttLightConfigured = false;  // the light profile gate (CTRL-03), until NET-20 decides
    SignalBars bars = 0;
    int16_t rssiDbm = -100;            // the driver's disconnected value

    bool operator==(const ConnectivityState& other) const {
        return wifiConnected == other.wifiConnected && internetAvailable == other.internetAvailable &&
               mqttConnected == other.mqttConnected && mqttLightConfigured == other.mqttLightConfigured &&
               bars == other.bars && rssiDbm == other.rssiDbm;
    }
    bool operator!=(const ConnectivityState& other) const { return !(*this == other); }
};

class ConnectivityModel : public ModelBase<ConnectivityState, 8> {
public:
    enum class Field : uint8_t {
        Wifi = 0,                // bool (never-drop: a transition must not be lost)
        Internet = 1,            // bool (never-drop)
        Mqtt = 2,                // bool (never-drop)
        Strength = 3,            // value = 0..4 (coalesce)
        Rssi = 4,                // value = dBm (coalesce)
        MqttLightConfigured = 5, // bool (never-drop)
        Count = 6
    };

    static RecordClass classOf(Field field);
    static ModelUpdate make(Field field, int32_t value, uint32_t atMs);
    bool applyUpdate(const ModelUpdate& update, Millis now);

    bool setWifiConnected(bool connected);
    bool setInternetAvailable(bool available);
    bool setMqttConnected(bool connected);
    bool setMqttLightConfigured(bool configured);
    bool setSignalStrength(int strength);  // 0..4 only; 5 is rejected
    bool setRssi(int dBm);                 // maps to bars with the driver's thresholds
};

}  // namespace model
