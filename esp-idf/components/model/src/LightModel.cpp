#include "model/LightModel.h"

#include "model/ColorMath.h"

namespace model {

namespace {

bool isNumber(float value) {
    return value == value && value <= 1.0e30f && value >= -1.0e30f;
}

float clampUnit(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

}  // namespace

LightModel::LightModel() : ModelBase(), publish_(kPublishIntervalMs) {}

RecordClass LightModel::classOf(Field field) {
    switch (field) {
        case Field::Brightness:
        case Field::BrightnessWire:
        case Field::Hue:
        case Field::Saturation:
            return RecordClass::CoalesceLatest;
        case Field::Mode:
        case Field::Count:
        default:
            return RecordClass::NeverDrop;
    }
}

ModelUpdate LightModel::make(Field field, int32_t value, uint32_t atMs) {
    return ModelUpdate::ofInt(Aggregate::Light, static_cast<uint8_t>(field), classOf(field), value, atMs);
}

ModelUpdate LightModel::makeFloat(Field field, float value, uint32_t atMs) {
    return ModelUpdate::ofFloat(Aggregate::Light, static_cast<uint8_t>(field), classOf(field), value, atMs);
}

bool LightModel::applyUpdate(const ModelUpdate& update, Millis now) {
    (void)now;
    switch (static_cast<Field>(update.field)) {
        case Field::Brightness:
            return setBrightness(update.value);
        case Field::BrightnessWire:
            return setBrightnessFromWire(update.value);
        case Field::Hue:
            return update.isFloat() && setHue(update.asFloat());
        case Field::Saturation:
            return update.isFloat() && setSaturation(update.asFloat());
        case Field::Mode:
            return setMode(static_cast<LedMode>(update.value));
        case Field::Count:
        default:
            return false;
    }
}

bool LightModel::setBrightness(int percent) {
    if (percent < 0 || percent > kPercentMax) {
        return false;
    }
    mutate().brightness = static_cast<Percent>(percent);
    return true;
}

bool LightModel::setBrightnessFromWire(int wire) {
    if (wire < 0 || wire > kWireMax) {
        return false;
    }
    return setBrightness(percentFromWire(wire));
}

bool LightModel::adjustBrightness(int delta) {
    const int next = clampPercent(static_cast<int>(state().brightness) + delta);
    if (next == state().brightness) {
        return false;
    }
    mutate().brightness = static_cast<Percent>(next);
    return true;
}

bool LightModel::setHue(float hue) {
    if (!isNumber(hue)) {
        return false;
    }
    mutate().hue = clampUnit(hue);
    return true;
}

bool LightModel::setSaturation(float saturation) {
    if (!isNumber(saturation)) {
        return false;
    }
    mutate().saturation = clampUnit(saturation);
    return true;
}

bool LightModel::setMode(LedMode mode) {
    if (mode >= LedMode::Count) {
        return false;
    }
    mutate().mode = mode;
    return true;
}

LedMode LightModel::cycleMode() {
    const LedMode next = static_cast<LedMode>((static_cast<int>(state().mode) + 1) % static_cast<int>(LedMode::Count));
    mutate().mode = next;
    return next;
}

int LightModel::wireFromPercent(int percent) {
    const int p = clampPercent(percent);
    return (p * kWireMax) / kPercentMax;  // 50 -> 127, as LightController always published
}

int LightModel::percentFromWire(int wire) {
    const int w = wire < 0 ? 0 : (wire > kWireMax ? kWireMax : wire);
    return (w * kPercentMax + kWireMax / 2) / kWireMax;  // 127 -> 50, 255 -> 100
}

uint32_t LightModel::publishedRgb() const {
    const LightState& s = state();
    return hsvToRgb(s.hue, s.saturation, static_cast<float>(s.brightness) / 100.0f);
}

uint32_t LightModel::swatchRgb() const {
    const LightState& s = state();
    return hsvToRgb(s.hue, s.saturation, 1.0f);
}

}  // namespace model
