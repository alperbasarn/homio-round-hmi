#include "model/ClimateModel.h"

namespace model {

namespace {

bool isFinite(float value) {
    return value == value && value <= 1.0e30f && value >= -1.0e30f;
}

}  // namespace

ClimateModel::ClimateModel() : ModelBase(), settle_(kSettleMs) {}  // the defaults already agree: 21 C is Comfort

RecordClass ClimateModel::classOf(Field field) {
    switch (field) {
        case Field::Target:
        case Field::Humidity:
        case Field::Pressure:
        case Field::IndoorSampled:
            return RecordClass::CoalesceLatest;
        case Field::TargetSteps:
        case Field::PublishPending:
        case Field::Count:
        default:
            return RecordClass::NeverDrop;
    }
}

ModelUpdate ClimateModel::make(Field field, int32_t value, uint32_t atMs) {
    return ModelUpdate::ofInt(Aggregate::Climate, static_cast<uint8_t>(field), classOf(field), value, atMs);
}

ModelUpdate ClimateModel::makeFloat(Field field, float value, uint32_t atMs) {
    return ModelUpdate::ofFloat(Aggregate::Climate, static_cast<uint8_t>(field), classOf(field), value, atMs);
}

bool ClimateModel::applyUpdate(const ModelUpdate& update, Millis now) {
    switch (static_cast<Field>(update.field)) {
        case Field::Target:
            return update.isFloat() && setTarget(update.asFloat());
        case Field::TargetSteps:
            return adjustTarget(update.value);
        case Field::Humidity:
            return update.isFloat() && setHumidity(update.asFloat());
        case Field::Pressure:
            return update.isFloat() && setPressure(update.asFloat());
        case Field::IndoorSampled:
            noteIndoorSample(now);
            return true;
        case Field::PublishPending:
            return setPublishPending(update.asBool());
        case Field::Count:
        default:
            return false;
    }
}

bool ClimateModel::setTarget(float celsius) {
    if (!isFinite(celsius)) {
        return false;
    }
    const float clamped = celsius < kTargetMinC ? kTargetMinC : (celsius > kTargetMaxC ? kTargetMaxC : celsius);
    mutate().targetTemp = clamped;
    trackBand();
    return true;
}

bool ClimateModel::adjustTarget(int halfDegreeSteps) {
    if (halfDegreeSteps == 0) {
        return false;
    }
    const float before = state().targetTemp;
    setTarget(before + static_cast<float>(halfDegreeSteps) * kTargetStepC);
    return state().targetTemp != before;
}

bool ClimateModel::setHumidity(float percent) {
    ClimateState& s = mutate();
    if (!isFinite(percent) || percent < 0.0f || percent > 100.0f) {
        s.humidityValid = false;
        return false;
    }
    s.humidityPct = percent;
    s.humidityValid = true;
    return true;
}

bool ClimateModel::setPressure(float hPa) {
    ClimateState& s = mutate();
    if (!isFinite(hPa) || hPa < 300.0f || hPa > 1100.0f) {
        s.pressureValid = false;
        return false;
    }
    s.pressureHpa = hPa;
    s.pressureValid = true;
    return true;
}

void ClimateModel::noteIndoorSample(Millis now) {
    mutate().indoorSampledAtMs = now;
}

bool ClimateModel::indoorFresh(Millis now) const {
    const Millis sampled = state().indoorSampledAtMs;
    if (sampled < 0) {
        return false;
    }
    return now - sampled <= kIndoorFreshMs;
}

void ClimateModel::noteTargetChanged(Millis now) {
    settle_.poke(now);
    mutate().publishPending = true;
}

bool ClimateModel::targetSettled(Millis now) {
    return settle_.settled(now);
}

bool ClimateModel::setPublishPending(bool pending) {
    mutate().publishPending = pending;
    return true;
}

void ClimateModel::trackBand() {
    ClimateState& s = mutate();
    const IndoorClass c = classifyIndoor(s.targetTemp);
    s.targetBand = c.band;
    s.targetProgress = c.progress;
}

}  // namespace model
