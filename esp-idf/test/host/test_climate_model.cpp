// test_climate_model (MDL-19): validity gating, NaN and infinity rejection, the 16..30 clamp,
// band tracking, the settle debounce, zero allocations.
#include <cmath>
#include <limits>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/ClimateModel.h"

using namespace model;

namespace {

void countNotify(const ClimateState&, void* context) { ++*static_cast<int*>(context); }

}  // namespace

TEST_CASE(sane_default_inside_16_to_30) {
    ClimateModel c;
    CHECK_EQ(c.state().targetTemp, 21.0f);
    CHECK(c.state().targetBand == IndoorBand::Comfort);
    CHECK(!c.state().humidityValid);
    CHECK(!c.state().pressureValid);
    CHECK(!c.state().publishPending);
    CHECK_EQ(c.state().indoorSampledAtMs, static_cast<Millis>(-1));
}

TEST_CASE(nan_and_infinity_are_rejected_and_never_stored) {
    ClimateModel c;
    CHECK(!c.setTarget(std::nanf("")));
    CHECK(!c.setTarget(std::numeric_limits<float>::infinity()));
    CHECK(!c.setTarget(-std::numeric_limits<float>::infinity()));
    CHECK_EQ(c.state().targetTemp, 21.0f);
    CHECK(!c.setHumidity(std::nanf("")));
    CHECK(!c.setPressure(std::numeric_limits<float>::infinity()));
}

TEST_CASE(target_clamps_to_16_and_30) {
    ClimateModel c;
    CHECK(c.setTarget(10.0f));
    CHECK_EQ(c.state().targetTemp, 16.0f);
    CHECK(c.state().targetBand == IndoorBand::Cold);
    CHECK(c.setTarget(35.0f));
    CHECK_EQ(c.state().targetTemp, 30.0f);
    CHECK(c.state().targetBand == IndoorBand::Hot);
    CHECK(!c.adjustTarget(1));       // already at the ceiling
    CHECK(c.adjustTarget(-1));
    CHECK_EQ(c.state().targetTemp, 29.5f);
    CHECK(!c.adjustTarget(0));
}

TEST_CASE(band_tracks_the_target) {
    ClimateModel c;
    CHECK(c.setTarget(18.0f));
    CHECK(c.state().targetBand == IndoorBand::Cold);
    CHECK(c.setTarget(22.0f));
    CHECK(c.state().targetBand == IndoorBand::Comfort);
    CHECK(c.setTarget(26.0f));
    CHECK(c.state().targetBand == IndoorBand::Warm);
    CHECK_EQ(c.state().targetProgress, 0.5f);
    CHECK(c.setTarget(29.0f));
    CHECK(c.state().targetBand == IndoorBand::Hot);
}

TEST_CASE(humidity_and_pressure_carry_validity) {
    ClimateModel c;
    CHECK(c.setHumidity(45.0f));
    CHECK(c.state().humidityValid);
    CHECK_EQ(c.state().humidityPct, 45.0f);
    CHECK(!c.setHumidity(120.0f));    // a malformed reading marks the field invalid
    CHECK(!c.state().humidityValid);
    CHECK(c.setPressure(1013.25f));
    CHECK(c.state().pressureValid);
    CHECK(!c.setPressure(50.0f));
    CHECK(!c.state().pressureValid);
}

TEST_CASE(target_settles_once_after_the_quiet_period_and_marks_a_publish) {
    ClimateModel c;
    c.noteTargetChanged(0);
    CHECK(c.state().publishPending);
    c.noteTargetChanged(100);
    CHECK(!c.targetSettled(499));
    CHECK(c.targetSettled(500));
    CHECK(!c.targetSettled(501));
    CHECK(c.setPublishPending(false));
    CHECK(!c.state().publishPending);
}

TEST_CASE(climate_model_allocates_nothing) {
    NO_HEAP_SCOPE();
    ClimateModel c;
    int notified = 0;
    CHECK(c.subscribe(&countNotify, &notified));
    for (int i = 0; i < 5000; ++i) {
        c.adjustTarget(i % 2 ? 1 : -1);
        c.setHumidity(static_cast<float>(i % 100));
        c.noteIndoorSample(i);
        c.commit();
    }
    CHECK(notified > 0);
}
