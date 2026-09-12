#include "adapters/ConnectivityPoller.h"

namespace adapters {

using Field = model::ConnectivityModel::Field;

void ConnectivityPollerCore::reset() {
    primed_ = false;
    last_ = ConnectivitySample{};
    lastBars_ = 0;
    posted_ = 0;
}

size_t ConnectivityPollerCore::poll(const ConnectivitySample& sample, model::Millis now, model::MutationQueue& queue) {
    const uint32_t atMs = static_cast<uint32_t>(now);
    const model::SignalBars bars = model::signalBarsFromRssi(sample.rssiDbm);
    size_t count = 0;

    if (!primed_ || sample.wifiConnected != last_.wifiConnected) {
        queue.post(model::ConnectivityModel::make(Field::Wifi, sample.wifiConnected ? 1 : 0, atMs));
        ++count;
    }
    if (!primed_ || sample.internetAvailable != last_.internetAvailable) {
        queue.post(model::ConnectivityModel::make(Field::Internet, sample.internetAvailable ? 1 : 0, atMs));
        ++count;
    }
    if (!primed_ || sample.mqttConnected != last_.mqttConnected) {
        queue.post(model::ConnectivityModel::make(Field::Mqtt, sample.mqttConnected ? 1 : 0, atMs));
        ++count;
    }
    if (!primed_ || sample.mqttLightConfigured != last_.mqttLightConfigured) {
        queue.post(model::ConnectivityModel::make(Field::MqttLightConfigured, sample.mqttLightConfigured ? 1 : 0, atMs));
        ++count;
    }
    if (!primed_ || bars != lastBars_) {
        queue.post(model::ConnectivityModel::make(Field::Rssi, sample.rssiDbm, atMs));
        ++count;
    }

    primed_ = true;
    last_ = sample;
    lastBars_ = bars;
    posted_ += static_cast<uint32_t>(count);
    return count;
}

}  // namespace adapters

#ifdef ESP_PLATFORM
#include "InternetHandler.h"
#include "MQTTManager.h"
#include "WiFiManager.h"

namespace adapters {

ConnectivityPoller::ConnectivityPoller(WiFiManager* wifi, InternetHandler* internet, MQTTManager* mqtt,
                                       model::MutationQueue& queue)
    : wifi_(wifi), internet_(internet), mqtt_(mqtt), queue_(queue), core_() {}

size_t ConnectivityPoller::poll(model::Millis now) {
    ConnectivitySample sample;
    sample.wifiConnected = wifi_ != nullptr && wifi_->isConnected();
    sample.internetAvailable = internet_ != nullptr && internet_->isInternetAvailable();
    sample.mqttConnected = mqtt_ != nullptr && mqtt_->isConnected();
    // The CTRL-03 gate: one client, the light topics go through it when either profile exists.
    sample.mqttLightConfigured = mqtt_ != nullptr && (mqtt_->hasLightMQTTConfigured() || mqtt_->hasSoundMQTTConfigured());
    sample.rssiDbm = sample.wifiConnected && wifi_ != nullptr ? wifi_->getRSSI() : -100;
    return core_.poll(sample, now, queue_);
}

}  // namespace adapters
#endif
