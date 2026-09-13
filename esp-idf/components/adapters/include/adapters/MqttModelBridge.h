#pragma once
// MqttModelBridge (LED-03, the device half): the LED family over the MQTT manager. The event task
// copies every `homio/led/...` message into a frame ring; the display task drains the ring into
// LedBridgeCore at the top of its frame and publishes what the core hands back after the drain.
// Console requests (select, pair, code, unpair) cross to the display task through atomics. The
// other topic families still go through the callback in main until Phase 5 moves them here.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "adapters/FrameRing.h"
#include "adapters/LedBridgeCore.h"
#include "adapters/LedTopics.h"
#include "model/AppModel.h"

class MQTTManager;

namespace adapters {

class MqttModelBridge {
public:
    static constexpr size_t kRingFrames = 12;
    static constexpr size_t kFrameTopic = 64;
    static constexpr size_t kFramePayload = 320;

    MqttModelBridge(model::AppModel& app, MQTTManager& mqtt);
    // Registers the four inbound subscriptions (taken on every connect) and keeps the pairing label.
    void begin(const char* pairingLabel);

    // MQTT event task. True when the topic is in the LED family (queued, or dropped and counted).
    bool onMqttMessage(const std::string& topic, const std::string& payload);
    // Display task, top of the frame, before the model drain.
    void pumpInbound(model::Millis now);
    // Display task, after the drain and the screen update.
    void pumpOutbound(model::Millis now);

    // Any task; consumed on the display task in pumpOutbound().
    void requestSelect(int slot);  // -1 clears
    void requestPair(int slot);
    void requestCode(int code);
    void requestUnpair(int slot);
    // Any task: one line per known strip from the registry snapshot, and the counters.
    int describeRegistry(char* out, size_t capacity) const;
    int describeStatus(char* out, size_t capacity) const;

    const char* ownId() const { return ownId_; }

private:
    const char* fillOwnId();
    static const char* pairingName(uint8_t state);

    model::AppModel& app_;
    MQTTManager& mqtt_;
    char ownId_[kLedControllerIdCapacity];
    char pairingLabel_[25];
    LedBridgeCore core_;
    FrameRing<kRingFrames, kFrameTopic, kFramePayload> ring_;
    std::atomic<int> pendingSelect_;
    std::atomic<int> pendingPair_;
    std::atomic<int> pendingCode_;
    std::atomic<int> pendingUnpair_;
    std::atomic<uint8_t> pairingState_;
    std::atomic<uint32_t> inbound_;
    std::atomic<uint32_t> publishes_;
    std::atomic<uint32_t> publishFailures_;
    bool refusedLogged_ = false;
    bool malformedLogged_ = false;
};

}  // namespace adapters
