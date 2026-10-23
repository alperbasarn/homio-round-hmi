#include "model/Bands.h"

namespace model {

float bandProgress(Celsius t, Celsius from, Celsius to) {
    const float raw = (t - from) / (to - from);
    return raw < 0.0f ? 0.0f : (raw > 1.0f ? 1.0f : raw);
}

IndoorClass classifyIndoor(Celsius t) {
    IndoorClass result;
    if (!isValidCelsius(t)) {
        return result;
    }
    if (t <= 18.0f) {
        result.band = IndoorBand::Cold;
        result.progress = bandProgress(t, 10.0f, 18.0f);   // (t - 10) / 8
    } else if (t <= 24.0f) {
        result.band = IndoorBand::Comfort;
    } else if (t <= 28.0f) {
        result.band = IndoorBand::Warm;
        result.progress = bandProgress(t, 24.0f, 28.0f);   // (t - 24) / 4
    } else {
        result.band = IndoorBand::Hot;
        result.progress = bandProgress(t, 28.0f, 36.0f);   // (t - 28) / 8
    }
    return result;
}

OutdoorClass classifyOutdoor(Celsius t) {
    OutdoorClass result;
    if (!isValidCelsius(t)) {
        return result;
    }
    if (t <= 0.0f) {
        result.band = OutdoorBand::Freezing;
    } else if (t <= 15.0f) {
        result.band = OutdoorBand::Cool;
        result.progress = bandProgress(t, 0.0f, 15.0f);    // t / 15
    } else if (t <= 28.0f) {
        result.band = OutdoorBand::Mild;
        result.progress = bandProgress(t, 15.0f, 28.0f);   // (t - 15) / 13
    } else {
        result.band = OutdoorBand::Hot;
        result.progress = bandProgress(t, 28.0f, 40.0f);   // (t - 28) / 12
    }
    return result;
}

BatteryBand classifyBattery(float percent) {
    if (!(percent == percent) || percent < 0.0f) {
        return BatteryBand::Invalid;
    }
    if (percent <= 20.0f) return BatteryBand::Critical;
    if (percent <= 40.0f) return BatteryBand::Low;
    if (percent <= 60.0f) return BatteryBand::Medium;
    return BatteryBand::Good;
}

SignalBars signalBarsFromRssi(int rssiDbm) {
    if (rssiDbm >= -50) return 4;
    if (rssiDbm >= -60) return 3;
    if (rssiDbm >= -70) return 2;
    if (rssiDbm >= -80) return 1;
    return 0;
}

SignalBars signalBarsFromStrength(int strength) {
    if (strength <= 0) return 0;
    if (strength >= kSignalBarsMax) return kSignalBarsMax;
    return static_cast<SignalBars>(strength);
}

}  // namespace model
