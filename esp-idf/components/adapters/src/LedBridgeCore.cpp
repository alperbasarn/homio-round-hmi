#include "adapters/LedBridgeCore.h"

#include <cstring>

#include "adapters/LedJson.h"

namespace adapters {

LedBridgeCore::LedBridgeCore(model::LedRegistryModel& registry, model::LightModel& light, const char* ownId)
    : registry_(registry), light_(light), ownId_(ownId != nullptr ? ownId : ""), label_("QNOB") {
    for (size_t i = 0; i < kQueue; ++i) {
        queue_[i].topic[0] = '\0';
        queue_[i].payload[0] = '\0';
    }
}

int LedBridgeCore::slotFor(const char* id, model::Millis now) {
    int slot = registry_.find(id);
    if (slot < 0) {
        slot = registry_.announce(id, "", now);
        if (slot < 0) {
            ++refused_;
        }
    }
    return slot;
}

bool LedBridgeCore::handleInbound(const char* topic, const char* payload, model::Millis now) {
    char id[model::kLedIdCapacity + 1];
    LedTopic kind = LedTopic::Unknown;
    if (!parseLedTopic(topic, id, sizeof(id), kind) || payload == nullptr) {
        return false;
    }
    if (payload[0] == '\0') {
        // A cleared retained topic (factory reset): the controller list is gone with it.
        if (kind == LedTopic::Controllers) {
            const int slot = registry_.find(id);
            if (slot >= 0) {
                registry_.setPaired(slot, false);
            }
        }
        return kind != LedTopic::Unknown;
    }
    switch (kind) {
        case LedTopic::Info: {
            LedInfoMsg m;
            if (!decodeLedInfo(payload, m)) {
                ++malformed_;
                return true;
            }
            if (registry_.announce(id, m.name.c_str(), now) < 0) {
                ++refused_;
            }
            return true;
        }
        case LedTopic::Status: {
            bool online = false;
            if (!decodeLedStatus(payload, online)) {
                ++malformed_;
                return true;
            }
            const int slot = slotFor(id, now);
            if (slot >= 0) {
                registry_.setOnline(slot, online, now);
            }
            return true;
        }
        case LedTopic::State: {
            LedStateMsg m;
            if (!decodeLedState(payload, ownId_.c_str(), m)) {
                ++malformed_;
                return true;
            }
            const int slot = slotFor(id, now);
            if (slot < 0) {
                return true;
            }
            // A field the document did not carry keeps what the registry knows.
            model::LedStripState next = registry_.slot(slot)->state;
            if (m.fields & LedStatePower) next.power = m.state.power;
            if (m.fields & LedStateBrightness) next.brightness = m.state.brightness;
            if (m.fields & LedStateColor) next.rgb = m.state.rgb;
            if (m.fields & LedStateEffect) next.effect = m.state.effect;
            if (m.fields & LedStateParams) std::memcpy(next.params, m.state.params, model::kLedParamCount);
            if (m.fields & LedStateEffectColor) next.effectRgb = m.state.effectRgb;
            if (m.fields & LedStateCount) next.count = m.state.count;
            if (m.rejected) {
                registry_.setPaired(slot, false);  // the strip does not obey this HMI
            }
            registry_.applyState(slot, next, m.source, m.seq, now);
            return true;
        }
        case LedTopic::Controllers: {
            LedControllersMsg m;
            if (!decodeLedControllers(payload, ownId_.c_str(), m)) {
                ++malformed_;
                return true;
            }
            const int slot = slotFor(id, now);
            if (slot < 0) {
                return true;
            }
            registry_.setPaired(slot, m.listsOwnId);
            registry_.noteSeen(slot, now);
            if (pairingSlot_ == slot && m.listsOwnId &&
                (pairing_ == Pairing::Requested || pairing_ == Pairing::Confirming)) {
                pairing_ = Pairing::Paired;
                registry_.setPairing(model::LedPairing::Paired, slot);  // the screen sees it this frame
            } else if (pairingSlot_ == slot && !m.listsOwnId && pairing_ == Pairing::Confirming) {
                // The strip republishes its unchanged list after a wrong code: the answer is no.
                pairing_ = Pairing::Failed;
                registry_.setPairing(model::LedPairing::Failed, slot);
            }
            return true;
        }
        case LedTopic::Set:
        case LedTopic::Pair:
        case LedTopic::Unpair:
            return true;  // ours to send; another controller's copy is not for us
        case LedTopic::Unknown:
        default:
            return true;  // in the family, an unknown leaf: ignored
    }
}

bool LedBridgeCore::enqueue(const char* id, LedTopic kind, const char* payload) {
    if (queueCount_ >= kQueue || id == nullptr || payload == nullptr) {
        return false;
    }
    Outbound& slot = queue_[(queueHead_ + queueCount_) % kQueue];
    if (formatLedTopic(slot.topic, kTopicCapacity, id, kind) < 0) {
        return false;
    }
    std::strncpy(slot.payload, payload, kPayloadCapacity - 1);
    slot.payload[kPayloadCapacity - 1] = '\0';
    ++queueCount_;
    return true;
}

bool LedBridgeCore::nextOutbound(Outbound& out, model::Millis now) {
    if (queueCount_ > 0) {
        out = queue_[queueHead_];
        queueHead_ = (queueHead_ + 1) % kQueue;
        --queueCount_;
        return true;
    }
    if (!light_.publishPending()) {
        return false;
    }
    const int slot = light_.state().stripSlot;
    if (!registry_.commandsAllowed(slot)) {
        light_.discardPending();  // refused locally, never queued (contract, section 3)
        return false;
    }
    if (!light_.shouldPublish(now)) {
        return false;
    }
    uint16_t fields = 0;
    const uint32_t seq = light_.takePending(fields);
    if (fields == 0) {
        return false;
    }
    if (formatLedTopic(out.topic, kTopicCapacity, registry_.slot(slot)->id.c_str(), LedTopic::Set) < 0) {
        return false;
    }
    if (encodeLedSet(out.payload, kPayloadCapacity, light_.asStripState(), fields, ownId_.c_str(), seq) < 0) {
        return false;
    }
    ++sets_;
    return true;
}

bool LedBridgeCore::startPairing(int slot, const char* label, model::Millis now) {
    const model::LedSlot* s = registry_.slot(slot);
    if (s == nullptr || pairing_ == Pairing::Requested || pairing_ == Pairing::Confirming) {
        return false;
    }
    char payload[kPayloadCapacity];
    if (encodeLedPair(payload, sizeof(payload), ownId_.c_str(), label) < 0 || !enqueue(s->id.c_str(), LedTopic::Pair, payload)) {
        return false;
    }
    pairing_ = Pairing::Requested;
    pairingSlot_ = slot;
    pairingSinceMs_ = now;
    return true;
}

bool LedBridgeCore::confirmPairing(int code, model::Millis now) {
    const model::LedSlot* s = registry_.slot(pairingSlot_);
    if (s == nullptr || pairing_ != Pairing::Requested) {
        return false;
    }
    char payload[kPayloadCapacity];
    if (encodeLedPairConfirm(payload, sizeof(payload), ownId_.c_str(), code) < 0 ||
        !enqueue(s->id.c_str(), LedTopic::Pair, payload)) {
        return false;
    }
    pairing_ = Pairing::Confirming;
    pairingSinceMs_ = now;
    return true;
}

bool LedBridgeCore::unpair(int slot) {
    const model::LedSlot* s = registry_.slot(slot);
    if (s == nullptr) {
        return false;
    }
    char payload[kPayloadCapacity];
    if (encodeLedUnpair(payload, sizeof(payload), ownId_.c_str()) < 0 || !enqueue(s->id.c_str(), LedTopic::Unpair, payload)) {
        return false;
    }
    registry_.setPaired(slot, false);
    if (pairingSlot_ == slot) {
        resetPairing();
    }
    return true;
}

void LedBridgeCore::tick(model::Millis now) {
    const int pairSlot = registry_.takePairIntent();
    if (pairSlot >= 0) {
        if (pairing_ == Pairing::Paired || pairing_ == Pairing::Failed) {
            resetPairing();  // a new request after a finished flow starts over
        }
        startPairing(pairSlot, label_.c_str(), now);
    }
    const int code = registry_.takeCodeIntent();
    if (code > 0) {
        confirmPairing(code, now);
    }
    const int unpairSlot = registry_.takeUnpairIntent();
    if (unpairSlot >= 0) {
        unpair(unpairSlot);
    }
    if ((pairing_ == Pairing::Requested || pairing_ == Pairing::Confirming) && now - pairingSinceMs_ > kPairWindowMs) {
        pairing_ = Pairing::Failed;
    }
    registry_.setPairing(static_cast<model::LedPairing>(pairing_), pairingSlot_);
}

}  // namespace adapters
