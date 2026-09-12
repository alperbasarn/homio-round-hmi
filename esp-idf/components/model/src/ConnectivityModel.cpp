#include "model/ConnectivityModel.h"

namespace model {

RecordClass ConnectivityModel::classOf(Field field) {
    switch (field) {
        case Field::Strength:
        case Field::Rssi:
            return RecordClass::CoalesceLatest;
        case Field::Wifi:
        case Field::Internet:
        case Field::Mqtt:
        case Field::MqttLightConfigured:
        case Field::Count:
        default:
            return RecordClass::NeverDrop;
    }
}

ModelUpdate ConnectivityModel::make(Field field, int32_t value, uint32_t atMs) {
    return ModelUpdate::ofInt(Aggregate::Connectivity, static_cast<uint8_t>(field), classOf(field), value, atMs);
}

bool ConnectivityModel::applyUpdate(const ModelUpdate& update, Millis now) {
    (void)now;
    switch (static_cast<Field>(update.field)) {
        case Field::Wifi:
            return setWifiConnected(update.asBool());
        case Field::Internet:
            return setInternetAvailable(update.asBool());
        case Field::Mqtt:
            return setMqttConnected(update.asBool());
        case Field::Strength:
            return setSignalStrength(update.value);
        case Field::Rssi:
            return setRssi(update.value);
        case Field::MqttLightConfigured:
            return setMqttLightConfigured(update.asBool());
        case Field::Count:
        default:
            return false;
    }
}

bool ConnectivityModel::setWifiConnected(bool connected) {
    mutate().wifiConnected = connected;
    return true;
}

bool ConnectivityModel::setInternetAvailable(bool available) {
    mutate().internetAvailable = available;
    return true;
}

bool ConnectivityModel::setMqttConnected(bool connected) {
    mutate().mqttConnected = connected;
    return true;
}

bool ConnectivityModel::setMqttLightConfigured(bool configured) {
    mutate().mqttLightConfigured = configured;
    return true;
}

bool ConnectivityModel::setSignalStrength(int strength) {
    if (strength < 0 || strength > kSignalBarsMax) {
        return false;
    }
    mutate().bars = static_cast<SignalBars>(strength);
    return true;
}

bool ConnectivityModel::setRssi(int dBm) {
    if (dBm < -127 || dBm > 20) {
        return false;
    }
    ConnectivityState& s = mutate();
    s.rssiDbm = static_cast<int16_t>(dBm);
    s.bars = signalBarsFromRssi(dBm);
    return true;
}

}  // namespace model
