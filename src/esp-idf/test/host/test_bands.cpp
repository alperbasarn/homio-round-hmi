// test_bands (MDL-08): every threshold, band + progress reproducing the old colour lerp bit for
// bit across a -20..50 sweep, the legacy -1.0f sentinel, NaN, and zero allocations.
#include <cmath>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/Bands.h"
#include "model/ColorMath.h"

using namespace model;

namespace {

// The old EnvironmentInfoScreen::getTemperatureColor, copied verbatim as the oracle.
uint8_t legacyLerp(uint8_t from, uint8_t to, float t) { return static_cast<uint8_t>(from + (to - from) * t); }
float legacyConstrain(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

Rgb legacyTemperatureRgb(float temperature, bool isIndoor) {
    uint8_t r = 124, g = 230, b = 90;
    if (isIndoor) {
        if (temperature <= 18.0f) {
            const float t = legacyConstrain((temperature - 10.0f) / 8.0f, 0.0f, 1.0f);
            r = legacyLerp(95, 124, t); g = legacyLerp(210, 230, t); b = legacyLerp(140, 90, t);
        } else if (temperature <= 24.0f) {
            r = 124; g = 230; b = 90;
        } else if (temperature <= 28.0f) {
            const float t = legacyConstrain((temperature - 24.0f) / 4.0f, 0.0f, 1.0f);
            r = legacyLerp(124, 255, t); g = legacyLerp(230, 191, t); b = legacyLerp(90, 74, t);
        } else {
            const float t = legacyConstrain((temperature - 28.0f) / 8.0f, 0.0f, 1.0f);
            r = 255; g = legacyLerp(191, 64, t); b = legacyLerp(74, 80, t);
        }
    } else {
        if (temperature <= 0.0f) {
            r = 68; g = 214; b = 255;
        } else if (temperature <= 15.0f) {
            const float t = legacyConstrain(temperature / 15.0f, 0.0f, 1.0f);
            r = legacyLerp(68, 124, t); g = legacyLerp(214, 230, t); b = legacyLerp(255, 90, t);
        } else if (temperature <= 28.0f) {
            const float t = legacyConstrain((temperature - 15.0f) / 13.0f, 0.0f, 1.0f);
            r = legacyLerp(124, 255, t); g = legacyLerp(230, 191, t); b = legacyLerp(90, 74, t);
        } else {
            const float t = legacyConstrain((temperature - 28.0f) / 12.0f, 0.0f, 1.0f);
            r = 255; g = legacyLerp(191, 64, t); b = legacyLerp(74, 80, t);
        }
    }
    return Rgb{r, g, b};
}

}  // namespace

TEST_CASE(indoor_thresholds_are_inclusive_at_the_old_boundaries) {
    CHECK(classifyIndoor(18.0f).band == IndoorBand::Cold);
    CHECK(classifyIndoor(18.01f).band == IndoorBand::Comfort);
    CHECK(classifyIndoor(24.0f).band == IndoorBand::Comfort);
    CHECK(classifyIndoor(24.01f).band == IndoorBand::Warm);
    CHECK(classifyIndoor(28.0f).band == IndoorBand::Warm);
    CHECK(classifyIndoor(28.01f).band == IndoorBand::Hot);
    CHECK_EQ(classifyIndoor(10.0f).progress, 0.0f);
    CHECK_EQ(classifyIndoor(18.0f).progress, 1.0f);
    CHECK_EQ(classifyIndoor(21.0f).progress, 0.0f);
}

TEST_CASE(outdoor_thresholds_are_inclusive_at_the_old_boundaries) {
    CHECK(classifyOutdoor(0.0f).band == OutdoorBand::Freezing);
    CHECK(classifyOutdoor(0.01f).band == OutdoorBand::Cool);
    CHECK(classifyOutdoor(15.0f).band == OutdoorBand::Cool);
    CHECK(classifyOutdoor(15.01f).band == OutdoorBand::Mild);
    CHECK(classifyOutdoor(28.0f).band == OutdoorBand::Mild);
    CHECK(classifyOutdoor(28.01f).band == OutdoorBand::Hot);
    CHECK(classifyOutdoor(-1.0f).band == OutdoorBand::Freezing);  // a real reading, not the sentinel
    CHECK_EQ(classifyOutdoor(40.0f).progress, 1.0f);
    CHECK_EQ(classifyOutdoor(60.0f).progress, 1.0f);
}

TEST_CASE(band_plus_progress_reproduces_the_old_lerp_bit_for_bit) {
    int mismatches = 0;
    for (int i = -200; i <= 500; ++i) {
        const float t = static_cast<float>(i) / 10.0f;
        if (indoorTemperatureRgb(classifyIndoor(t)) != legacyTemperatureRgb(t, true)) ++mismatches;
        if (outdoorTemperatureRgb(classifyOutdoor(t)) != legacyTemperatureRgb(t, false)) ++mismatches;
    }
    CHECK_EQ(mismatches, 0);
}

TEST_CASE(sentinels_and_nan_classify_as_invalid) {
    CHECK(classifyIndoor(kNoTemperature).band == IndoorBand::Invalid);
    CHECK(classifyOutdoor(kNoTemperature).band == OutdoorBand::Invalid);
    CHECK(classifyIndoor(legacyTemperature(-1.0f)).band == IndoorBand::Invalid);
    CHECK(classifyIndoor(std::nanf("")).band == IndoorBand::Invalid);
    CHECK(classifyOutdoor(std::nanf("")).band == OutdoorBand::Invalid);
    CHECK(classifyIndoor(-300.0f).band == IndoorBand::Invalid);
    CHECK(legacyTemperature(21.5f) == 21.5f);
    CHECK(indoorTemperatureRgb(classifyIndoor(kNoTemperature)) == kNeutralGrey);
}

TEST_CASE(battery_bands_and_sentinel) {
    CHECK(classifyBattery(-1.0f) == BatteryBand::Invalid);
    CHECK(classifyBattery(std::nanf("")) == BatteryBand::Invalid);
    CHECK(classifyBattery(0.0f) == BatteryBand::Critical);
    CHECK(classifyBattery(20.0f) == BatteryBand::Critical);
    CHECK(classifyBattery(20.1f) == BatteryBand::Low);
    CHECK(classifyBattery(40.0f) == BatteryBand::Low);
    CHECK(classifyBattery(40.1f) == BatteryBand::Medium);
    CHECK(classifyBattery(60.0f) == BatteryBand::Medium);
    CHECK(classifyBattery(60.1f) == BatteryBand::Good);
    CHECK(classifyBattery(100.0f) == BatteryBand::Good);
    CHECK(batteryRgb(BatteryBand::Critical).packed() == 0xFF4444u);
    CHECK(batteryRgb(BatteryBand::Low).packed() == 0xFFAA00u);
    CHECK(batteryRgb(BatteryBand::Medium).packed() == 0xFFDD44u);
    CHECK(batteryRgb(BatteryBand::Good).packed() == 0x7BE85Au);
}

TEST_CASE(signal_bars_reach_all_five_values) {
    CHECK_EQ(signalBarsFromRssi(-30), 4);
    CHECK_EQ(signalBarsFromRssi(-50), 4);
    CHECK_EQ(signalBarsFromRssi(-51), 3);
    CHECK_EQ(signalBarsFromRssi(-60), 3);
    CHECK_EQ(signalBarsFromRssi(-70), 2);
    CHECK_EQ(signalBarsFromRssi(-80), 1);
    CHECK_EQ(signalBarsFromRssi(-81), 0);
    CHECK_EQ(signalBarsFromRssi(-100), 0);
    CHECK_EQ(signalBarsFromStrength(5), kSignalBarsMax);
    CHECK_EQ(signalBarsFromStrength(-3), 0);
    CHECK_EQ(signalBarsFromStrength(4), 4);
}

TEST_CASE(classification_allocates_nothing) {
    NO_HEAP_SCOPE();
    int hot = 0;
    for (int i = 0; i < 10000; ++i) {
        const float t = static_cast<float>(i % 60) - 10.0f;
        if (classifyIndoor(t).band == IndoorBand::Hot) ++hot;
        (void)classifyOutdoor(t);
        (void)classifyBattery(static_cast<float>(i % 101));
    }
    CHECK(hot > 0);
}
