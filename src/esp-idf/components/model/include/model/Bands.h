#pragma once
// Domain bands (MDL-07). The thresholds are the ones the views used to hide inside their colour
// functions; the views now map (band, progress) to a colour and own nothing numeric.
#include <cstdint>

#include "model/ModelTypes.h"

namespace model {

// Indoor: <=18 cold, <=24 comfort, <=28 warm, else hot (EnvironmentInfoScreen::getTemperatureColor).
enum class IndoorBand : uint8_t { Invalid, Cold, Comfort, Warm, Hot };
// Outdoor: <=0 freezing, <=15 cool, <=28 mild, else hot.
enum class OutdoorBand : uint8_t { Invalid, Freezing, Cool, Mild, Hot };
// Battery percent: <=20 critical, <=40 low, <=60 medium, else good (the DEVICE_INFO battery colour).
enum class BatteryBand : uint8_t { Invalid, Critical, Low, Medium, Good };

// 0..4 bars. The old screen accepted 0..3 and showed the fourth bar for anything above, so the
// top bar was unreachable; the driver's own mapping (WiFiManager::getSignalStrength) is 0..4.
using SignalBars = uint8_t;
constexpr SignalBars kSignalBarsMax = 4;

// A band plus the position inside it, 0..1, so a graded band can still be lerped by the view
// exactly as before. Flat bands report 0.
struct IndoorClass {
    IndoorBand band = IndoorBand::Invalid;
    float progress = 0.0f;
};
struct OutdoorClass {
    OutdoorBand band = OutdoorBand::Invalid;
    float progress = 0.0f;
};

// (t - from) / (to - from), clamped to 0..1. Endpoints are chosen so the divisor is the exact
// literal the old code used (8, 4, 8 indoor; 15, 13, 12 outdoor).
float bandProgress(Celsius t, Celsius from, Celsius to);

IndoorClass classifyIndoor(Celsius t);    // kNoTemperature, NaN and impossible values give Invalid
OutdoorClass classifyOutdoor(Celsius t);  // note: -1.0f is a real outdoor temperature here
BatteryBand classifyBattery(float percent);  // < 0 (the old -1.0f sentinel) and NaN give Invalid

// The old code used -1.0f for "no reading". Translate it once at the boundary, never inside a
// model: a real -1 C must still classify.
constexpr Celsius legacyTemperature(float value) {
    return value == -1.0f ? kNoTemperature : value;
}

SignalBars signalBarsFromRssi(int rssiDbm);      // -50/-60/-70/-80 thresholds -> 4/3/2/1/0
SignalBars signalBarsFromStrength(int strength);  // clamps an already-mapped value to 0..4

}  // namespace model
