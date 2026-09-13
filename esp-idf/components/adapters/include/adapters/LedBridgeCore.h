#pragma once
// LedBridgeCore (LED-03 / LED-04, the pure half of the MQTT bridge for the LED family): routes
// inbound `info`, `status`, `state` and `controllers` into the registry, turns the pending
// LightModel fields into one `set` per 100 ms window for the selected strip, and runs the pairing
// flow. It knows no client: the device wrapper feeds it strings on the display task and publishes
// whatever nextOutbound() hands back. No heap: fixed buffers, a four-deep control queue.
#include <cstddef>
#include <cstdint>

#include "adapters/LedTopics.h"
#include "model/LedRegistryModel.h"
#include "model/LightModel.h"
#include "model/ModelTypes.h"

namespace adapters {

class LedBridgeCore {
public:
    static constexpr model::Millis kPairWindowMs = 60000;
    static constexpr size_t kTopicCapacity = kLedTopicCapacity;
    static constexpr size_t kPayloadCapacity = 256;
    static constexpr size_t kQueue = 4;

    enum class Pairing : uint8_t { Idle = 0, Requested, Confirming, Paired, Failed };

    struct Outbound {
        char topic[kTopicCapacity];
        char payload[kPayloadCapacity];
    };

    LedBridgeCore(model::LedRegistryModel& registry, model::LightModel& light, const char* ownId);

    // Inbound, on the display task. True when the topic belongs to the family (applied or counted
    // as malformed); false for anything else, so the wrapper can pass it on.
    bool handleInbound(const char* topic, const char* payload, model::Millis now);
    // The next message to publish, or false. Control messages first, then one `set` when the
    // window allows and the selected strip is online and mine; pending changes for a strip that
    // may not be commanded are discarded (no queueing, per the contract).
    bool nextOutbound(Outbound& out, model::Millis now);

    // The pairing flow (LED-04): a request opens the window on the strip, the code the user dialled
    // confirms it, `controllers` naming this HMI completes it, 60 s without that fails it.
    // The label a pair request carries (the device name); the intents from the registry use it.
    void setPairingLabel(const char* label) { label_.assign(label != nullptr ? label : "QNOB"); }
    bool startPairing(int slot, const char* label, model::Millis now);
    bool confirmPairing(int code, model::Millis now);
    bool unpair(int slot);
    // Services the pairing intents posted to the registry, runs the 60 s timeout, and writes the
    // flow state back into the registry for the screen. Once per frame, before nextOutbound().
    void tick(model::Millis now);
    Pairing pairing() const { return pairing_; }
    int pairingSlot() const { return pairingSlot_; }
    void resetPairing() { pairing_ = Pairing::Idle; pairingSlot_ = -1; }

    const char* ownId() const { return ownId_.c_str(); }
    uint32_t malformedCount() const { return malformed_; }
    uint32_t refusedAnnounceCount() const { return refused_; }
    uint32_t setsPublished() const { return sets_; }

private:
    bool enqueue(const char* id, LedTopic kind, const char* payload);
    int slotFor(const char* id, model::Millis now);  // announce on demand for a strip seen first by state

    model::LedRegistryModel& registry_;
    model::LightModel& light_;
    model::FixedString<kLedControllerIdCapacity> ownId_;
    model::FixedString<24> label_;
    Outbound queue_[kQueue];
    size_t queueHead_ = 0;
    size_t queueCount_ = 0;
    Pairing pairing_ = Pairing::Idle;
    int pairingSlot_ = -1;
    model::Millis pairingSinceMs_ = 0;
    uint32_t malformed_ = 0;
    uint32_t refused_ = 0;
    uint32_t sets_ = 0;
};

}  // namespace adapters
