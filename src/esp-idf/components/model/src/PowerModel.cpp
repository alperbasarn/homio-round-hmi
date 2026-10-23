#include "model/PowerModel.h"

#include "model/PowerRules.h"

namespace model {

namespace {

bool isReading(float value) {
    return value == value && value >= 0.0f && value <= 1.0e6f;
}

}  // namespace

RecordClass PowerModel::classOf(Field field) {
    switch (field) {
        case Field::Percent:
        case Field::Voltage:
        case Field::CurrentMa:
            return RecordClass::CoalesceLatest;
        case Field::Connected:
        case Field::PresenceKnown:
        case Field::Charging:
        case Field::Count:
        default:
            return RecordClass::NeverDrop;
    }
}

ModelUpdate PowerModel::make(Field field, int32_t value, uint32_t atMs) {
    return ModelUpdate::ofInt(Aggregate::Power, static_cast<uint8_t>(field), classOf(field), value, atMs);
}

ModelUpdate PowerModel::makeFloat(Field field, float value, uint32_t atMs) {
    return ModelUpdate::ofFloat(Aggregate::Power, static_cast<uint8_t>(field), classOf(field), value, atMs);
}

bool PowerModel::applyUpdate(const ModelUpdate& update, Millis now) {
    (void)now;
    switch (static_cast<Field>(update.field)) {
        case Field::Percent:
            return update.isFloat() && setPercent(update.asFloat());
        case Field::Voltage:
            return update.isFloat() && setVoltage(update.asFloat());
        case Field::CurrentMa:
            return update.isFloat() && setCurrentMa(update.asFloat());
        case Field::Connected:
            return setPresence(state().presenceKnown, update.asBool());
        case Field::PresenceKnown:
            return setPresence(update.asBool(), state().connected);
        case Field::Charging:
            return setCharging(update.asBool());
        case Field::Count:
        default:
            return false;
    }
}

bool PowerModel::setPercent(float percent) {
    PowerState& s = mutate();
    if (!isReading(percent)) {
        s.percent = -1.0f;
        s.percentValid = false;
        s.band = BatteryBand::Invalid;
        return false;
    }
    s.percent = percent > 100.0f ? 100.0f : percent;
    s.percentValid = true;
    s.band = classifyBattery(s.percent);
    return true;
}

bool PowerModel::setVoltage(float volts) {
    PowerState& s = mutate();
    if (!isValidCellVoltage(volts)) {
        s.voltage = -1.0f;
        s.voltageValid = false;
        return false;
    }
    s.voltage = volts;
    s.voltageValid = true;
    return true;
}

bool PowerModel::setCurrentMa(float milliamps) {
    PowerState& s = mutate();
    if (!(milliamps == milliamps) || milliamps < -1.0e5f || milliamps > 1.0e5f || milliamps == -1.0f) {
        s.currentMa = -1.0f;
        s.currentValid = false;
        return false;
    }
    s.currentMa = milliamps;
    s.currentValid = true;
    return true;
}

bool PowerModel::derivePercentFromVoltage() {
    if (!state().voltageValid) {
        return false;
    }
    return setPercent(socPercentFromVoltage(state().voltage));
}

bool PowerModel::setPresence(bool presenceKnown, bool connected) {
    PowerState& s = mutate();
    s.presenceKnown = presenceKnown;
    s.connected = connected;
    if (presenceKnown && !connected) {
        s.charging = false;
    }
    return true;
}

bool PowerModel::setCharging(bool charging) {
    mutate().charging = charging;
    return true;
}

}  // namespace model
