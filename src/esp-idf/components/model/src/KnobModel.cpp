#include "model/KnobModel.h"

namespace model {

namespace {

bool isFinite(float value) {
    return value == value && value <= 1.0e30f && value >= -1.0e30f;
}

}  // namespace

bool KnobState::operator==(const KnobState& other) const {
    return detentCount == other.detentCount && linkAlive == other.linkAlive &&
           lastLinkActivityMs == other.lastLinkActivityMs && haptic == other.haptic && context == other.context &&
           accumulator == other.accumulator && lastDirection == other.lastDirection &&
           lastDetentMs == other.lastDetentMs && pendingWrite == other.pendingWrite &&
           writeSentAtMs == other.writeSentAtMs && droppedBursts == other.droppedBursts &&
           clampedBursts == other.clampedBursts && writesAcked == other.writesAcked &&
           writeTimeouts == other.writeTimeouts;
}

RecordClass KnobModel::classOf(Field field) {
    switch (field) {
        case Field::Burst:
        case Field::LinkActivity:
        case Field::Poles:
        case Field::Strength:
        case Field::Snap:
        case Field::VoltageLimit:
            return RecordClass::CoalesceLatest;
        case Field::Delta:
        case Field::WriteAck:
        case Field::Count:
        default:
            return RecordClass::NeverDrop;
    }
}

ModelUpdate KnobModel::make(Field field, int32_t value, uint32_t atMs) {
    return ModelUpdate::ofInt(Aggregate::Knob, static_cast<uint8_t>(field), classOf(field), value, atMs);
}

ModelUpdate KnobModel::makeFloat(Field field, float value, uint32_t atMs) {
    return ModelUpdate::ofFloat(Aggregate::Knob, static_cast<uint8_t>(field), classOf(field), value, atMs);
}

bool KnobModel::applyUpdate(const ModelUpdate& update, Millis now) {
    switch (static_cast<Field>(update.field)) {
        case Field::Delta:
            applyDelta(update.value, now);
            return true;
        case Field::Burst:
            burstToDelta(update.value, now);
            return true;
        case Field::LinkActivity:
            noteLinkActivity(now);
            return true;
        case Field::WriteAck:
            return noteWriteAck(update.value);
        case Field::Poles:
            return setPoles(update.value);
        case Field::Strength:
            return update.isFloat() && setDetentStrength(update.asFloat());
        case Field::Snap:
            return update.isFloat() && setSnapPoint(update.asFloat());
        case Field::VoltageLimit:
            return update.isFloat() && setVoltageLimit(update.asFloat());
        case Field::Count:
        default:
            return false;
    }
}

int KnobModel::clampCount(int value) {
    return value < kCountMin ? kCountMin : (value > kCountMax ? kCountMax : value);
}

int KnobModel::divisorFor(KnobContext context) {
    switch (context) {
        case KnobContext::Wheel: return 4;
        case KnobContext::TargetToggle: return 3;
        case KnobContext::Volume: return 1;
        case KnobContext::ClimateHalfDegree: return 1;
        case KnobContext::SeekFine: return 1;
        case KnobContext::SeekCoarse: return 3;
        case KnobContext::Count: break;
    }
    return 1;
}

int KnobModel::applyDelta(int detents, Millis now) {
    noteLinkActivity(now);
    if (detents == 0) {
        return 0;
    }
    KnobState& s = mutate();
    s.detentCount = static_cast<int16_t>(clampCount(s.detentCount + detents));
    return stepsFor(detents, now);
}

int KnobModel::burstToDelta(int absoluteCount, Millis now) {
    noteLinkActivity(now);
    const int target = clampCount(absoluteCount);
    KnobState& s = mutate();
    if (s.pendingWrite >= 0) {
        ++s.droppedBursts;  // relative to the base the knob had before our write: stale
        return 0;
    }
    const int raw = target - s.detentCount;
    const int delta = raw < -kBurstClamp ? -kBurstClamp : (raw > kBurstClamp ? kBurstClamp : raw);
    if (delta != raw) {
        ++s.clampedBursts;
    }
    s.detentCount = static_cast<int16_t>(target);
    return delta;
}

int KnobModel::stepsFor(int detents, Millis now) {
    if (detents == 0) {
        return 0;
    }
    KnobState& s = mutate();
    const int8_t direction = detents > 0 ? 1 : -1;
    if (s.lastDetentMs >= 0 && now - s.lastDetentMs > kIdleResetMs) {
        s.accumulator = 0;
    }
    if (s.lastDirection != 0 && direction != s.lastDirection) {
        s.accumulator = 0;
    }
    s.accumulator = static_cast<int16_t>(s.accumulator + detents);
    s.lastDirection = direction;
    s.lastDetentMs = now;
    const int divisor = divisorFor(s.context);
    const int steps = s.accumulator / divisor;  // truncates toward zero
    s.accumulator = static_cast<int16_t>(s.accumulator - steps * divisor);
    return steps;
}

void KnobModel::resetAccumulator() {
    KnobState& s = mutate();
    s.accumulator = 0;
    s.lastDirection = 0;
    s.lastDetentMs = -1;
}

void KnobModel::setContext(KnobContext context) {
    if (context >= KnobContext::Count) {
        return;
    }
    mutate().context = context;
    resetAccumulator();
}

void KnobModel::onEnter(KnobContext context, int modeValue, Millis now) {
    setContext(context);
    requestWrite(modeValue, now);
}

bool KnobModel::requestWrite(int value, Millis now) {
    (void)now;
    KnobState& s = mutate();
    s.pendingWrite = static_cast<int16_t>(clampCount(value));
    s.writeSentAtMs = -1;
    return true;
}

int KnobModel::takeWriteRequest(Millis now) {
    const KnobState& cur = state();
    if (cur.pendingWrite < 0 || cur.writeSentAtMs >= 0) {
        return -1;  // nothing new to send
    }
    mutate().writeSentAtMs = now;
    return cur.pendingWrite;
}

bool KnobModel::noteWriteAck(int value) {
    KnobState& s = mutate();
    if (s.pendingWrite < 0 || value != s.pendingWrite) {
        return false;
    }
    s.detentCount = s.pendingWrite;
    s.pendingWrite = -1;
    s.writeSentAtMs = -1;
    ++s.writesAcked;
    return true;
}

void KnobModel::tick(Millis now) {
    const KnobState& cur = state();
    if (cur.pendingWrite >= 0 && cur.writeSentAtMs >= 0 && now - cur.writeSentAtMs > kWriteAckTimeoutMs) {
        KnobState& s = mutate();
        s.detentCount = s.pendingWrite;  // assume the write landed; the next burst is relative to it
        s.pendingWrite = -1;
        s.writeSentAtMs = -1;
        ++s.writeTimeouts;
    }
    const bool alive = linkAlive(now);
    if (alive != state().linkAlive) {
        mutate().linkAlive = alive;
    }
}

void KnobModel::noteLinkActivity(Millis now) {
    KnobState& s = mutate();
    s.lastLinkActivityMs = now;
    s.linkAlive = true;
}

bool KnobModel::linkAlive(Millis now) const {
    const Millis last = state().lastLinkActivityMs;
    if (last < 0) {
        return false;
    }
    return now - last <= kLinkTimeoutMs;
}

bool KnobModel::setHaptic(const HapticConfig& config) {
    return setPoles(config.poles) && setDetentStrength(config.detentStrength) && setSnapPoint(config.snapPoint) &&
           setVoltageLimit(config.voltageLimit);
}

bool KnobModel::setPoles(int poles) {
    if (poles < 1 || poles > 1000) {
        return false;
    }
    mutate().haptic.poles = static_cast<int16_t>(poles);
    return true;
}

bool KnobModel::setDetentStrength(float strength) {
    if (!isFinite(strength) || strength < 0.0f || strength > 5.0f) {
        return false;
    }
    mutate().haptic.detentStrength = strength;
    return true;
}

bool KnobModel::setSnapPoint(float snap) {
    if (!isFinite(snap) || snap < 0.5f || snap > 1.0f) {
        return false;
    }
    mutate().haptic.snapPoint = snap;
    return true;
}

bool KnobModel::setVoltageLimit(float volts) {
    if (!isFinite(volts) || volts < 0.5f || volts > 12.0f) {
        return false;
    }
    mutate().haptic.voltageLimit = volts;
    return true;
}

}  // namespace model
