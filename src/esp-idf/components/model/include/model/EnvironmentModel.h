#pragma once
// EnvironmentModel (MDL-20): what the INFO screen shows. Fixed-character date/time/day text
// derived from an epoch (the pollers post numbers, never strings), both temperatures with
// validity bits and their bands, and the 0.5 C hysteresis EnvironmentInfoScreen applied.
#include <cstdint>

#include "model/Bands.h"
#include "model/DateFormat.h"
#include "model/ModelBase.h"
#include "model/ModelTypes.h"
#include "model/MutationQueue.h"

namespace model {

struct EnvironmentState {
    FixedString<10> date{"----/--/--"};  // "YYYY/MM/DD", the form the old formatDate() consumed
    FixedString<5> time{"--:--"};        // "HH:MM"; the view blinks the colon
    FixedString<3> day{"---"};           // "MON".."SUN"
    DateText formatted{"-- ---"};        // "7 FEB"
    bool clockValid = false;
    int32_t epochDay = -1;     // days since 1970-01-01, local
    int16_t minuteOfDay = -1;  // 0..1439, local
    int16_t utcOffsetMinutes = 0;
    float indoorTemp = 18.0f;  // the old screen's defaults, shown until a reading arrives
    bool indoorValid = false;
    float outdoorTemp = 0.0f;
    bool outdoorValid = false;
    IndoorClass indoorClass;
    OutdoorClass outdoorClass;

    bool operator==(const EnvironmentState& other) const;
    bool operator!=(const EnvironmentState& other) const { return !(*this == other); }
};

class EnvironmentModel : public ModelBase<EnvironmentState, 8> {
public:
    enum class Field : uint8_t {
        Epoch = 0,        // value = epoch seconds (UTC), aux = UTC offset in minutes (coalesce)
        IndoorTemp = 1,   // float (coalesce)
        OutdoorTemp = 2,  // float (coalesce)
        Count = 3
    };

    static constexpr float kHysteresisC = 0.5f;

    EnvironmentModel();

    static RecordClass classOf(Field field);
    static ModelUpdate make(Field field, int32_t value, uint32_t atMs, int32_t aux = 0);
    static ModelUpdate makeFloat(Field field, float value, uint32_t atMs);
    bool applyUpdate(const ModelUpdate& update, Millis now);

    // Local civil time from a UTC epoch and an offset. Mutates only when the minute changes
    // (the poller's minute-granularity filter is enforced here too). Negative local time is
    // rejected.
    bool setClock(int64_t epochSeconds, int utcOffsetMinutes);
    // The legacy string path for the shadow phase: the strings truncate to their capacity.
    bool setLegacyDateTime(const char* slashDate, const char* hhmm, const char* dayText);

    // Admitted on the first write regardless of value (a true 18.0 C is a reading, not the
    // default), then only when it moves by more than kHysteresisC. Invalid values are rejected.
    bool setIndoorTemp(Celsius t);
    bool setOutdoorTemp(Celsius t);

    static bool civilFromDays(int64_t days, int& year, int& month, int& day, int& weekday);
    static const char* dayName(int weekday);  // 0 = Sunday

private:
    void classify();
};

}  // namespace model
