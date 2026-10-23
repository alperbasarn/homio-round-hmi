#pragma once
// PowerModel (MDL-24): battery state with explicit validity. The old -1.0f sentinel becomes
// percentValid / voltageValid = false here; "presence known" and "connected" are separate,
// because an AXP2101 can know that no cell is fitted.
#include <cstdint>

#include "model/Bands.h"
#include "model/ModelBase.h"
#include "model/ModelTypes.h"
#include "model/MutationQueue.h"

namespace model {

struct PowerState {
    float percent = -1.0f;
    bool percentValid = false;
    float voltage = -1.0f;
    bool voltageValid = false;
    float currentMa = -1.0f;
    bool currentValid = false;
    bool connected = false;
    bool presenceKnown = false;
    bool charging = false;
    BatteryBand band = BatteryBand::Invalid;

    bool operator==(const PowerState& other) const {
        return percent == other.percent && percentValid == other.percentValid && voltage == other.voltage &&
               voltageValid == other.voltageValid && currentMa == other.currentMa &&
               currentValid == other.currentValid && connected == other.connected &&
               presenceKnown == other.presenceKnown && charging == other.charging && band == other.band;
    }
    bool operator!=(const PowerState& other) const { return !(*this == other); }
};

class PowerModel : public ModelBase<PowerState, 8> {
public:
    enum class Field : uint8_t {
        Percent = 0,        // float; -1 means no reading (coalesce)
        Voltage = 1,        // float; -1 means no reading (coalesce)
        CurrentMa = 2,      // float; -1 means no reading (coalesce)
        Connected = 3,      // bool (never-drop)
        PresenceKnown = 4,  // bool (never-drop)
        Charging = 5,       // bool (never-drop)
        Count = 6
    };

    static RecordClass classOf(Field field);
    static ModelUpdate make(Field field, int32_t value, uint32_t atMs);
    static ModelUpdate makeFloat(Field field, float value, uint32_t atMs);
    bool applyUpdate(const ModelUpdate& update, Millis now);

    // -1 (or NaN) clears the reading: percentValid false and no band. Returns false for that.
    bool setPercent(float percent);
    bool setVoltage(float volts);
    bool setCurrentMa(float milliamps);
    // When only a voltage is known, derive the percent from the SoC curve.
    bool derivePercentFromVoltage();
    bool setPresence(bool presenceKnown, bool connected);
    bool setCharging(bool charging);
};

}  // namespace model
