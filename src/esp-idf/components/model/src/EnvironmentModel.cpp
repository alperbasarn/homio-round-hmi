#include "model/EnvironmentModel.h"

#include <cstdio>

namespace model {

namespace {

constexpr const char* kDays[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

float absDiff(float a, float b) {
    const float d = a - b;
    return d < 0.0f ? -d : d;
}

}  // namespace

bool EnvironmentState::operator==(const EnvironmentState& other) const {
    return date == other.date && time == other.time && day == other.day && formatted == other.formatted &&
           clockValid == other.clockValid && epochDay == other.epochDay && minuteOfDay == other.minuteOfDay &&
           utcOffsetMinutes == other.utcOffsetMinutes && indoorTemp == other.indoorTemp &&
           indoorValid == other.indoorValid && outdoorTemp == other.outdoorTemp &&
           outdoorValid == other.outdoorValid && indoorClass.band == other.indoorClass.band &&
           indoorClass.progress == other.indoorClass.progress && outdoorClass.band == other.outdoorClass.band &&
           outdoorClass.progress == other.outdoorClass.progress;
}

EnvironmentModel::EnvironmentModel() : ModelBase() {}

RecordClass EnvironmentModel::classOf(Field field) {
    (void)field;
    return RecordClass::CoalesceLatest;  // every environment field is a sample; the latest wins
}

ModelUpdate EnvironmentModel::make(Field field, int32_t value, uint32_t atMs, int32_t aux) {
    return ModelUpdate::ofInt(Aggregate::Environment, static_cast<uint8_t>(field), classOf(field), value, atMs, aux);
}

ModelUpdate EnvironmentModel::makeFloat(Field field, float value, uint32_t atMs) {
    return ModelUpdate::ofFloat(Aggregate::Environment, static_cast<uint8_t>(field), classOf(field), value, atMs);
}

bool EnvironmentModel::applyUpdate(const ModelUpdate& update, Millis now) {
    (void)now;
    switch (static_cast<Field>(update.field)) {
        case Field::Epoch:
            return setClock(update.value, update.aux);
        case Field::IndoorTemp:
            return update.isFloat() && setIndoorTemp(update.asFloat());
        case Field::OutdoorTemp:
            return update.isFloat() && setOutdoorTemp(update.asFloat());
        case Field::Count:
        default:
            return false;
    }
}

// Howard Hinnant's civil-from-days, valid for every date the device can see.
bool EnvironmentModel::civilFromDays(int64_t days, int& year, int& month, int& day, int& weekday) {
    if (days < 0 || days > 2932896) {  // up to 9999-12-31
        return false;
    }
    const int64_t z = days + 719468;
    const int64_t era = z / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const int64_t m = mp < 10 ? mp + 3 : mp - 9;
    year = static_cast<int>(m <= 2 ? y + 1 : y);
    month = static_cast<int>(m);
    day = static_cast<int>(d);
    weekday = static_cast<int>((days + 4) % 7);  // 1970-01-01 was a Thursday
    return true;
}

const char* EnvironmentModel::dayName(int weekday) {
    if (weekday < 0 || weekday > 6) {
        return "---";
    }
    return kDays[weekday];
}

bool EnvironmentModel::setClock(int64_t epochSeconds, int utcOffsetMinutes) {
    const int64_t local = epochSeconds + static_cast<int64_t>(utcOffsetMinutes) * 60;
    if (local < 0) {
        return false;
    }
    const int64_t days = local / 86400;
    const int minute = static_cast<int>((local % 86400) / 60);
    const EnvironmentState& cur = state();
    if (cur.clockValid && cur.epochDay == days && cur.minuteOfDay == minute &&
        cur.utcOffsetMinutes == utcOffsetMinutes) {
        return false;  // same minute: nothing to redraw
    }
    int year = 0;
    int month = 0;
    int day = 0;
    int weekday = 0;
    if (!civilFromDays(days, year, month, day, weekday)) {
        return false;
    }
    // Scratch buffers wide enough for any int (the cross build enables -Wformat-truncation);
    // FixedString::assign truncates to the display width anyway.
    char dateBuffer[32];
    char timeBuffer[16];
    EnvironmentState& s = mutate();
    std::snprintf(dateBuffer, sizeof(dateBuffer), "%04d/%02d/%02d", year, month, day);
    s.date.assign(dateBuffer);
    std::snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d", minute / 60, minute % 60);
    s.time.assign(timeBuffer);
    s.day.assign(dayName(weekday));
    formatDate(s.date.c_str(), s.formatted);
    s.clockValid = true;
    s.epochDay = static_cast<int32_t>(days);
    s.minuteOfDay = static_cast<int16_t>(minute);
    s.utcOffsetMinutes = static_cast<int16_t>(utcOffsetMinutes);
    return true;
}

bool EnvironmentModel::setLegacyDateTime(const char* slashDate, const char* hhmm, const char* dayText) {
    EnvironmentState next = state();
    next.date.assign(slashDate);
    next.time.assign(hhmm);
    next.day.assign(dayText);
    next.clockValid = formatDate(next.date.c_str(), next.formatted);
    if (next == state()) {
        return false;
    }
    mutate() = next;
    return true;
}

bool EnvironmentModel::setIndoorTemp(Celsius t) {
    if (!isValidCelsius(t)) {
        return false;
    }
    const EnvironmentState& cur = state();
    if (cur.indoorValid && absDiff(t, cur.indoorTemp) <= kHysteresisC) {
        return false;
    }
    EnvironmentState& s = mutate();
    s.indoorTemp = t;
    s.indoorValid = true;
    classify();
    return true;
}

bool EnvironmentModel::setOutdoorTemp(Celsius t) {
    if (!isValidCelsius(t)) {
        return false;
    }
    const EnvironmentState& cur = state();
    if (cur.outdoorValid && absDiff(t, cur.outdoorTemp) <= kHysteresisC) {
        return false;
    }
    EnvironmentState& s = mutate();
    s.outdoorTemp = t;
    s.outdoorValid = true;
    classify();
    return true;
}

void EnvironmentModel::classify() {
    EnvironmentState& s = mutate();
    s.indoorClass = s.indoorValid ? classifyIndoor(s.indoorTemp) : IndoorClass{};
    s.outdoorClass = s.outdoorValid ? classifyOutdoor(s.outdoorTemp) : OutdoorClass{};
}

}  // namespace model
