#include "model/LightModel.h"

#include <cstring>

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

bool LightState::operator==(const LightState& other) const {
    return power == other.power && brightness == other.brightness && hue == other.hue && rgb == other.rgb &&
           effect == other.effect && std::memcmp(params, other.params, sizeof(params)) == 0 &&
           effectRgb == other.effectRgb && stripSlot == other.stripSlot;
}

LightModel::LightModel() : ModelBase(), publish_(kPublishIntervalMs) {}

RecordClass LightModel::classOf(Field field) {
    switch (field) {
        case Field::Brightness:
        case Field::BrightnessWire:
        case Field::Hue:
        case Field::Color:
        case Field::Params:
        case Field::EffectColor:
            return RecordClass::CoalesceLatest;
        case Field::Power:
        case Field::Effect:
        case Field::EffectStep:
        case Field::BrightnessDelta:
        case Field::Count:
        default:
            return RecordClass::NeverDrop;
    }
}

ModelUpdate LightModel::make(Field field, int32_t value, uint32_t atMs, int32_t aux) {
    return ModelUpdate::ofInt(Aggregate::Light, static_cast<uint8_t>(field), classOf(field), value, atMs, aux);
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
        case Field::Power:
            return setPower(update.asBool());
        case Field::Effect:
            return update.value >= 0 && update.value < static_cast<int32_t>(LedEffect::Count) &&
                   setEffect(static_cast<LedEffect>(update.value));
        case Field::EffectStep:
            stepEffect(update.value);
            return true;
        case Field::Color:
            return setColor(static_cast<uint32_t>(update.value));
        case Field::Params: {
            uint8_t p[kLedParamCount];
            const uint32_t v = static_cast<uint32_t>(update.value);
            p[0] = static_cast<uint8_t>(v & 0xFFu);
            p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
            p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
            p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
            p[4] = static_cast<uint8_t>(static_cast<uint32_t>(update.aux) & 0xFFu);
            return setParams(p);
        }
        case Field::EffectColor:
            return setEffectColor(static_cast<uint32_t>(update.value));
        case Field::BrightnessDelta:
            adjustBrightness(update.value);
            return true;
        case Field::Count:
        default:
            return false;
    }
}

void LightModel::markLocal(uint16_t bit) {
    pending_ |= bit;
    publish_.markChanged();
}

bool LightModel::setPower(bool on) {
    if (state().power != on) {
        mutate().power = on;
        markLocal(PendingPower);
    }
    return true;
}

bool LightModel::setBrightness(int percent) {
    if (percent < 0 || percent > kPercentMax) {
        return false;
    }
    if (state().brightness != percent) {
        mutate().brightness = static_cast<Percent>(percent);
        markLocal(PendingBrightness);
    }
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
    markLocal(PendingBrightness);
    return true;
}

bool LightModel::setHue(float hue) {
    if (!isNumber(hue)) {
        return false;
    }
    const float clamped = clampUnit(hue);
    const uint32_t rgb = rgbFromHue(clamped);
    if (state().hue != clamped || state().rgb != rgb) {
        LightState& s = mutate();
        s.hue = clamped;
        s.rgb = rgb;
        markLocal(PendingColor);
    }
    return true;
}

bool LightModel::setColor(uint32_t rgb) {
    const uint32_t masked = rgb & 0xFFFFFFu;
    if (state().rgb != masked) {
        LightState& s = mutate();
        s.rgb = masked;
        s.hue = rgbToHue(masked);
        markLocal(PendingColor);
    }
    return true;
}

bool LightModel::setEffect(LedEffect effect) {
    if (effect >= LedEffect::Count) {
        return false;
    }
    if (state().effect != effect) {
        mutate().effect = effect;
        markLocal(PendingEffect);
    }
    return true;
}

LedEffect LightModel::stepEffect(int steps) {
    const LedEffect next = stepLedEffect(state().effect, steps);
    if (next != state().effect) {
        mutate().effect = next;
        markLocal(PendingEffect);
    }
    return next;
}

bool LightModel::setParams(const uint8_t params[kLedParamCount]) {
    if (params == nullptr) {
        return false;
    }
    if (std::memcmp(state().params, params, kLedParamCount) != 0) {
        std::memcpy(mutate().params, params, kLedParamCount);
        markLocal(PendingParams);
    }
    return true;
}

bool LightModel::setEffectColor(uint32_t rgb) {
    const uint32_t masked = rgb & 0xFFFFFFu;
    if (state().effectRgb != masked) {
        mutate().effectRgb = masked;
        markLocal(PendingEffectColor);
    }
    return true;
}

bool LightModel::follow(int slot, const LedStripState* reported) {
    if (slot < -1 || slot >= static_cast<int>(LedRegistryState::kSlots)) {
        return false;
    }
    LightState next = state();
    next.stripSlot = static_cast<int8_t>(slot);
    if (reported != nullptr) {
        next.power = reported->power;
        next.brightness = reported->brightness;
        next.rgb = reported->rgb & 0xFFFFFFu;
        next.hue = rgbToHue(next.rgb);
        next.effect = reported->effect;
        std::memcpy(next.params, reported->params, kLedParamCount);
        next.effectRgb = reported->effectRgb & 0xFFFFFFu;
    }
    pending_ = 0;
    publish_.reset();
    if (next != state()) {
        mutate() = next;
    }
    return true;
}

bool LightModel::adoptRemote(const LedStripState& s, LedSource source, uint32_t seq) {
    const bool echo = source == LedSource::Self && seq <= sentSeq_;
    if (echo && seq < sentSeq_) {
        return false;  // a stale echo of an older set; newer ones are in flight
    }
    // An echo respects fields with a newer local change still pending; a remote change wins.
    const uint16_t keep = echo ? pending_ : 0u;
    LightState next = state();
    if (!(keep & PendingPower)) next.power = s.power;
    if (!(keep & PendingBrightness)) next.brightness = s.brightness;
    if (!(keep & PendingColor) && next.rgb != (s.rgb & 0xFFFFFFu)) {
        next.rgb = s.rgb & 0xFFFFFFu;
        next.hue = rgbToHue(next.rgb);
    }
    if (!(keep & PendingEffect)) next.effect = s.effect;
    if (!(keep & PendingParams)) std::memcpy(next.params, s.params, kLedParamCount);
    if (!(keep & PendingEffectColor)) next.effectRgb = s.effectRgb & 0xFFFFFFu;
    if (!echo) {
        pending_ = 0;
    }
    if (next == state()) {
        return false;
    }
    mutate() = next;
    return true;
}

uint32_t LightModel::takePending(uint16_t& fields) {
    fields = pending_;
    pending_ = 0;
    if (fields != 0) {
        ++sentSeq_;
    }
    return sentSeq_;
}

LedStripState LightModel::asStripState() const {
    LedStripState out;
    const LightState& s = state();
    out.power = s.power;
    out.brightness = s.brightness;
    out.rgb = s.rgb;
    out.effect = s.effect;
    std::memcpy(out.params, s.params, kLedParamCount);
    out.effectRgb = s.effectRgb;
    out.count = 0;
    return out;
}

int LightModel::wireFromPercent(int percent) {
    const int p = clampPercent(percent);
    return (p * kWireMax) / kPercentMax;  // 50 -> 127, as the old ring publish did
}

int LightModel::percentFromWire(int wire) {
    const int w = wire < 0 ? 0 : (wire > kWireMax ? kWireMax : wire);
    return (w * kPercentMax + kWireMax / 2) / kWireMax;  // 127 -> 50, 255 -> 100
}

uint32_t LightModel::publishedRgb() const {
    const LightState& s = state();
    return hsvToRgb(s.hue, 1.0f, static_cast<float>(s.brightness) / 100.0f);
}

}  // namespace model
