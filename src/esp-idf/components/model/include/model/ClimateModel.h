#pragma once
// ClimateModel (MDL-18, D4). Owns only what the CLIMATE screen controls or that nothing else
// holds: the target, humidity and pressure with validity, and when the indoor reading was last
// sampled. The indoor and outdoor temperatures stay in EnvironmentModel.
#include <cstdint>

#include "model/Bands.h"
#include "model/ModelBase.h"
#include "model/ModelTypes.h"
#include "model/MutationQueue.h"
#include "model/RateLimit.h"

namespace model {

struct ClimateState {
    float targetTemp = 21.0f;                     // a sane default inside 16..30
    IndoorBand targetBand = IndoorBand::Comfort;
    float targetProgress = 0.0f;
    float humidityPct = 0.0f;
    bool humidityValid = false;
    float pressureHpa = 0.0f;
    bool pressureValid = false;
    Millis indoorSampledAtMs = -1;                // -1 until the first indoor sample
    bool publishPending = false;                  // the settled target still has to go out

    bool operator==(const ClimateState& other) const {
        return targetTemp == other.targetTemp && targetBand == other.targetBand &&
               targetProgress == other.targetProgress && humidityPct == other.humidityPct &&
               humidityValid == other.humidityValid && pressureHpa == other.pressureHpa &&
               pressureValid == other.pressureValid && indoorSampledAtMs == other.indoorSampledAtMs &&
               publishPending == other.publishPending;
    }
    bool operator!=(const ClimateState& other) const { return !(*this == other); }
};

class ClimateModel : public ModelBase<ClimateState, 8> {
public:
    enum class Field : uint8_t {
        Target = 0,          // float Celsius (coalesce)
        TargetSteps = 1,     // value = half-degree steps, signed (never-drop: a detent is not lost)
        Humidity = 2,        // float percent (coalesce)
        Pressure = 3,        // float hPa (coalesce)
        IndoorSampled = 4,   // no operand; stamps indoorSampledAtMs with the drain time (coalesce)
        PublishPending = 5,  // value = bool (never-drop)
        Count = 6
    };

    static constexpr float kTargetMinC = 16.0f;
    static constexpr float kTargetMaxC = 30.0f;
    static constexpr float kTargetStepC = 0.5f;
    static constexpr Millis kIndoorFreshMs = 120000;  // the design's 120 s stale-sensor gate
    static constexpr Millis kSettleMs = 400;          // publish the value the knob settled on

    ClimateModel();

    static RecordClass classOf(Field field);
    static ModelUpdate make(Field field, int32_t value, uint32_t atMs);
    static ModelUpdate makeFloat(Field field, float value, uint32_t atMs);
    bool applyUpdate(const ModelUpdate& update, Millis now);

    bool setTarget(float celsius);       // NaN and infinities rejected; clamped to 16..30
    bool adjustTarget(int halfDegreeSteps);
    bool setHumidity(float percent);     // 0..100 valid; anything else marks invalid and returns false
    bool setPressure(float hPa);         // 300..1100 valid; anything else marks invalid and returns false
    void noteIndoorSample(Millis now);
    bool indoorFresh(Millis now) const;  // sampled, and no older than kIndoorFreshMs

    // Target debounce: poke on every change, settled() fires once after kSettleMs of quiet.
    void noteTargetChanged(Millis now);
    bool targetSettled(Millis now);
    bool setPublishPending(bool pending);

private:
    void trackBand();
    Debouncer settle_;
};

}  // namespace model
