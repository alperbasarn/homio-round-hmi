#pragma once
// ConnectivityPoller (NET-06). Runs on networkTask; reads the link state the drivers know and
// posts a record only when something changed (push-on-change). RSSI becomes SignalBars here,
// with the model's thresholds, so the driver's own mapping is no longer consulted.
//
// The core is pure and host-tested (NET-31); the driver-facing wrapper only exists on the
// device.
#include <cstddef>
#include <cstdint>

#include "model/ConnectivityModel.h"
#include "model/MutationQueue.h"

namespace adapters {

struct ConnectivitySample {
    bool wifiConnected = false;
    bool internetAvailable = false;
    bool mqttConnected = false;
    bool mqttLightConfigured = false;
    int rssiDbm = -100;
};

class ConnectivityPollerCore {
public:
    // Posts one record per changed field (the first call posts every field) and returns how
    // many were posted. A steady sample posts nothing; RSSI jitter inside one bar posts nothing.
    size_t poll(const ConnectivitySample& sample, model::Millis now, model::MutationQueue& queue);
    uint32_t posted() const { return posted_; }
    void reset();

private:
    bool primed_ = false;
    ConnectivitySample last_{};
    model::SignalBars lastBars_ = 0;
    uint32_t posted_ = 0;
};

}  // namespace adapters

#ifdef ESP_PLATFORM
class WiFiManager;
class InternetHandler;
class MQTTManager;

namespace adapters {

class ConnectivityPoller {
public:
    ConnectivityPoller(WiFiManager* wifi, InternetHandler* internet, MQTTManager* mqtt, model::MutationQueue& queue);
    size_t poll(model::Millis now);

private:
    WiFiManager* wifi_;
    InternetHandler* internet_;
    MQTTManager* mqtt_;
    model::MutationQueue& queue_;
    ConnectivityPollerCore core_;
};

}  // namespace adapters
#endif
