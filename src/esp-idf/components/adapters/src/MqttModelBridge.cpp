#include "adapters/MqttModelBridge.h"

#include <cstdio>
#include <cstring>

#include "MQTTManager.h"
#include "esp_log.h"
#include "esp_mac.h"

namespace adapters {

namespace {
const char* TAG = "LedBridge";
}  // namespace

const char* MqttModelBridge::fillOwnId() {
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK || !makeControllerId(mac, ownId_, sizeof(ownId_))) {
        std::snprintf(ownId_, sizeof(ownId_), "qnob-000000");
    }
    return ownId_;
}

MqttModelBridge::MqttModelBridge(model::AppModel& app, MQTTManager& mqtt)
    : app_(app),
      mqtt_(mqtt),
      core_(app.ledRegistry(), app.light(), fillOwnId()),
      ring_(),
      inbound_(0),
      publishes_(0),
      publishFailures_(0) {
    pairingLabel_[0] = '\0';
}

void MqttModelBridge::begin(const char* pairingLabel) {
    std::strncpy(pairingLabel_, pairingLabel != nullptr ? pairingLabel : "QNOB", sizeof(pairingLabel_) - 1);
    pairingLabel_[sizeof(pairingLabel_) - 1] = '\0';
    core_.setPairingLabel(pairingLabel_);
    const LedTopic inbound[] = {LedTopic::Info, LedTopic::Status, LedTopic::State, LedTopic::Controllers};
    char pattern[kLedTopicCapacity];
    for (LedTopic kind : inbound) {
        if (formatLedSubscription(pattern, sizeof(pattern), kind) > 0) {
            mqtt_.addSubscription(pattern, 1);
        }
    }
    ESP_LOGI(TAG, "LED bridge up: controller id %s, label %s", ownId_, pairingLabel_);
}

bool MqttModelBridge::onMqttMessage(const std::string& topic, const std::string& payload) {
    const size_t prefixLen = std::strlen(kLedTopicPrefix);
    if (topic.size() <= prefixLen || topic.compare(0, prefixLen, kLedTopicPrefix) != 0) {
        return false;
    }
    ring_.push(topic.data(), topic.size(), payload.data(), payload.size());
    return true;
}

void MqttModelBridge::pumpInbound(model::Millis now) {
    FrameRing<kRingFrames, kFrameTopic, kFramePayload>::Frame frame;
    while (ring_.pop(frame)) {
        inbound_.fetch_add(1, std::memory_order_relaxed);
        core_.handleInbound(frame.topic, frame.payload, now);
    }
}

void MqttModelBridge::pumpOutbound(model::Millis now) {
    core_.tick(now);  // pairing intents from the registry, the timeout, the flow state back
    if (!mqtt_.isConnected()) {
        return;  // pending sets and queued control messages wait for the broker
    }
    LedBridgeCore::Outbound out;
    while (core_.nextOutbound(out, now)) {
        if (mqtt_.publishRaw(out.topic, out.payload, std::strlen(out.payload), 1, false) == ESP_OK) {
            publishes_.fetch_add(1, std::memory_order_relaxed);
        } else {
            publishFailures_.fetch_add(1, std::memory_order_relaxed);
            app_.light().restorePending(out.fields);  // a set that never left keeps its fields pending
            break;
        }
    }
    if (!refusedLogged_ && core_.refusedAnnounceCount() > 0) {
        refusedLogged_ = true;
        ESP_LOGW(TAG, "a ninth strip announced itself; the registry holds eight");
    }
    if (!malformedLogged_ && core_.malformedCount() > 0) {
        malformedLogged_ = true;
        ESP_LOGW(TAG, "a malformed LED document was ignored (counted from here on)");
    }
}

int MqttModelBridge::describeRegistry(char* out, size_t capacity) const {
    if (out == nullptr || capacity == 0) {
        return -1;
    }
    const model::LedRegistryState s = app_.ledRegistry().readSnapshot();
    int n = std::snprintf(out, capacity, "leds: %u known, selected %d\n", static_cast<unsigned>(s.used), static_cast<int>(s.selected));
    for (size_t i = 0; i < model::LedRegistryState::kSlots && n >= 0 && static_cast<size_t>(n) < capacity; ++i) {
        const model::LedSlot& slot = s.slots[i];
        if (!slot.used) {
            continue;
        }
        n += std::snprintf(out + n, capacity - static_cast<size_t>(n), "  %u %s \"%s\" %s %s pwr=%d b=%u fx=%s rgb=%06lx n=%u\n",
                           static_cast<unsigned>(i), slot.id.c_str(), slot.name.c_str(), slot.online ? "online" : "offline",
                           slot.paired ? "mine" : "available", slot.state.power ? 1 : 0,
                           static_cast<unsigned>(slot.state.brightness), model::ledEffectName(slot.state.effect),
                           static_cast<unsigned long>(slot.state.rgb), static_cast<unsigned>(slot.state.count));
    }
    return n;
}

int MqttModelBridge::describeStatus(char* out, size_t capacity) const {
    if (out == nullptr || capacity == 0) {
        return -1;
    }
    const model::LedRegistryState s = app_.ledRegistry().readSnapshot();
    return std::snprintf(out, capacity, "ledStatus: id=%s pairing=%s slot=%d inbound=%lu dropped=%lu oversized=%lu high=%u publishes=%lu failed=%lu",
                         ownId_, model::ledPairingName(s.pairing), static_cast<int>(s.pairingSlot),
                         static_cast<unsigned long>(inbound_.load(std::memory_order_relaxed)),
                         static_cast<unsigned long>(ring_.dropped()), static_cast<unsigned long>(ring_.oversized()),
                         static_cast<unsigned>(ring_.highWater()),
                         static_cast<unsigned long>(publishes_.load(std::memory_order_relaxed)),
                         static_cast<unsigned long>(publishFailures_.load(std::memory_order_relaxed)));
}

}  // namespace adapters
