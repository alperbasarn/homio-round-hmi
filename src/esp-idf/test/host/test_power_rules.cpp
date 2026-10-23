// test_power_rules (PERI-03 part; MDL-25 extends it with PowerModel): the 13 curve knees, three
// midpoints, the clamps above 4.20 V and below 3.30 V, and the sentinels.
#include <cmath>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/PowerModel.h"
#include "model/PowerRules.h"

using namespace model;

namespace {

bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

}  // namespace

TEST_CASE(the_thirteen_knees_return_their_table_values) {
    const float volts[13] = {4.20f, 4.11f, 4.03f, 3.95f, 3.88f, 3.82f, 3.77f, 3.72f, 3.67f, 3.61f, 3.54f, 3.45f, 3.30f};
    const float percent[13] = {100.0f, 90.0f, 80.0f, 70.0f, 60.0f, 50.0f, 40.0f, 30.0f, 20.0f, 12.0f, 7.0f, 3.0f, 0.0f};
    for (int i = 0; i < 13; ++i) {
        CHECK(near(socPercentFromVoltage(volts[i]), percent[i]));
    }
}

TEST_CASE(three_midpoints_interpolate_linearly) {
    CHECK(near(socPercentFromVoltage(4.155f), 95.0f));   // between 4.11 (90) and 4.20 (100)
    CHECK(near(socPercentFromVoltage(3.85f), 55.0f));    // between 3.82 (50) and 3.88 (60)
    CHECK(near(socPercentFromVoltage(3.745f), 35.0f));   // between 3.72 (30) and 3.77 (40)
}

TEST_CASE(clamps_above_full_and_below_empty) {
    CHECK(near(socPercentFromVoltage(4.35f), 100.0f));
    CHECK(near(socPercentFromVoltage(kCellFullVolts), 100.0f));
    CHECK(near(socPercentFromVoltage(3.20f), 0.0f));
    CHECK(near(socPercentFromVoltage(kCellEmptyVolts), 0.0f));
}

TEST_CASE(sentinels_are_invalid_not_empty) {
    CHECK(!isValidCellVoltage(-1.0f));
    CHECK(!isValidCellVoltage(std::nanf("")));
    CHECK(!isValidCellVoltage(12.0f));
    CHECK(isValidCellVoltage(3.7f));
    CHECK(near(socPercentFromVoltage(-1.0f), -1.0f));   // carried through, never 0 % "empty"
    CHECK(near(socPercentFromVoltage(std::nanf("")), -1.0f));
}

TEST_CASE(curve_allocates_nothing) {
    NO_HEAP_SCOPE();
    float acc = 0.0f;
    for (int i = 0; i < 10000; ++i) {
        acc += socPercentFromVoltage(3.0f + static_cast<float>(i % 150) / 100.0f);
    }
    CHECK(acc > 0.0f);
}

// MDL-25: PowerModel on top of the curve. The old -1.0f sentinel becomes explicit validity;
// presence known and connected are separate facts.

namespace {

void countPower(const PowerState&, void* context) { ++*static_cast<int*>(context); }

}  // namespace

TEST_CASE(sentinel_sets_percent_invalid_and_yields_no_band) {
    PowerModel p;
    CHECK(!p.state().percentValid);
    CHECK(p.state().band == BatteryBand::Invalid);
    CHECK(!p.setPercent(-1.0f));
    CHECK(!p.state().percentValid);
    CHECK(p.state().band == BatteryBand::Invalid);
    CHECK(p.setPercent(55.0f));
    CHECK(p.state().percentValid);
    CHECK(p.state().band == BatteryBand::Medium);
    CHECK(!p.setPercent(std::nanf("")));
    CHECK(!p.state().percentValid);
    CHECK(p.setPercent(140.0f));
    CHECK_EQ(p.state().percent, 100.0f);
}

TEST_CASE(presence_known_is_not_connected) {
    PowerModel p;
    CHECK(!p.state().presenceKnown);
    CHECK(!p.state().connected);
    CHECK(p.setCharging(true));
    CHECK(p.setPresence(true, false));     // the AXP2101 knows no cell is fitted
    CHECK(p.state().presenceKnown);
    CHECK(!p.state().connected);
    CHECK(!p.state().charging);            // nothing charges an absent cell
    CHECK(p.setPresence(true, true));
    CHECK(p.state().connected);
}

TEST_CASE(percent_can_be_derived_from_a_valid_voltage) {
    PowerModel p;
    CHECK(!p.derivePercentFromVoltage());  // no voltage yet
    CHECK(!p.setVoltage(-1.0f));
    CHECK(!p.state().voltageValid);
    CHECK(p.setVoltage(3.85f));
    CHECK(p.state().voltageValid);
    CHECK(p.derivePercentFromVoltage());
    CHECK(near(p.state().percent, 55.0f));
    CHECK(p.state().band == BatteryBand::Medium);
    CHECK(!p.setCurrentMa(-1.0f));
    CHECK(!p.state().currentValid);
    CHECK(p.setCurrentMa(120.0f));
    CHECK(p.state().currentValid);
}

TEST_CASE(power_model_allocates_nothing) {
    NO_HEAP_SCOPE();
    PowerModel p;
    int notified = 0;
    CHECK(p.subscribe(&countPower, &notified));
    for (int i = 0; i < 10000; ++i) {
        p.setVoltage(3.3f + static_cast<float>(i % 90) / 100.0f);
        p.derivePercentFromVoltage();
        p.commit();
    }
    CHECK(notified > 0);
}
