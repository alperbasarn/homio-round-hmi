// test_pollers (NET-31): push-on-change versus steady state, RSSI to bars with every bar value
// reachable, the minute-granularity clock filter, the outdoor temperature on change only, the
// battery sentinel passing through to an invalid reading, all against sample structs and a real
// queue drained into an AppModel, with no ESP header in the translation unit.
#include "HeapGuard.h"
#include "TestRunner.h"
#include "adapters/BatteryPoller.h"
#include "adapters/ConnectivityPoller.h"
#include "adapters/EnvironmentPoller.h"
#include "model/AppModel.h"

using namespace adapters;
using namespace model;

namespace {

constexpr int64_t kFeb7_2026_1234Utc = 1770467640;

}  // namespace

TEST_CASE(connectivity_posts_every_field_once_then_only_changes) {
    NO_HEAP_SCOPE();
    AppModel app;
    ConnectivityPollerCore core;
    ConnectivitySample sample;
    sample.wifiConnected = true;
    sample.rssiDbm = -55;
    CHECK_EQ(core.poll(sample, 0, app.mutations()), 5u);   // primed: all five fields
    CHECK_EQ(core.poll(sample, 100, app.mutations()), 0u);  // steady: nothing
    sample.rssiDbm = -58;                                   // still three bars
    CHECK_EQ(core.poll(sample, 200, app.mutations()), 0u);
    sample.rssiDbm = -61;                                   // two bars
    CHECK_EQ(core.poll(sample, 300, app.mutations()), 1u);
    sample.mqttConnected = true;
    sample.internetAvailable = true;
    CHECK_EQ(core.poll(sample, 400, app.mutations()), 2u);
    app.drainPendingMutations(500);
    CHECK(app.connectivity().state().wifiConnected);
    CHECK(app.connectivity().state().mqttConnected);
    CHECK(app.connectivity().state().internetAvailable);
    CHECK_EQ(static_cast<int>(app.connectivity().state().bars), 2);
    CHECK_EQ(app.connectivity().state().rssiDbm, -61);
    CHECK_EQ(app.rejectedRecords(), 0u);
}

TEST_CASE(connectivity_reaches_all_five_bar_values) {
    NO_HEAP_SCOPE();
    AppModel app;
    ConnectivityPollerCore core;
    ConnectivitySample sample;
    sample.wifiConnected = true;
    const int rssi[5] = {-30, -55, -65, -75, -90};
    int distinct = 0;
    int lastBars = -1;
    for (int i = 0; i < 5; ++i) {
        sample.rssiDbm = rssi[i];
        core.poll(sample, i * 100, app.mutations());
        app.drainPendingMutations(i * 100 + 50);
        const int bars = app.connectivity().state().bars;
        if (bars != lastBars) ++distinct;
        lastBars = bars;
    }
    CHECK_EQ(distinct, 5);
    CHECK_EQ(lastBars, 0);
}

TEST_CASE(environment_posts_the_clock_once_per_minute_and_never_a_string) {
    NO_HEAP_SCOPE();
    AppModel app;
    EnvironmentPollerCore core;
    EnvironmentSample sample;
    CHECK_EQ(core.poll(sample, 0, app.mutations()), 0u);     // clock not valid yet
    sample.clockValid = true;
    sample.epochSeconds = kFeb7_2026_1234Utc;
    sample.utcOffsetMinutes = 180;
    CHECK_EQ(core.poll(sample, 0, app.mutations()), 1u);
    sample.epochSeconds += 30;
    CHECK_EQ(core.poll(sample, 100, app.mutations()), 0u);   // same minute
    sample.epochSeconds += 30;
    CHECK_EQ(core.poll(sample, 200, app.mutations()), 1u);   // next minute
    app.drainPendingMutations(300);
    CHECK(app.environment().state().clockValid);
    CHECK_EQ(static_cast<int>(app.environment().state().minuteOfDay), 15 * 60 + 35);

    // Ten simulated minutes at the 100 ms poll cadence: one clock record per minute boundary.
    // The window starts inside the minute that was just posted, so nine boundaries fall in it.
    uint32_t before = core.posted();
    for (int tick = 0; tick < 6000; ++tick) {
        sample.epochSeconds = kFeb7_2026_1234Utc + 60 + tick / 10;
        core.poll(sample, 300 + tick * 100, app.mutations());
        if (tick % 50 == 0) app.drainPendingMutations(300 + tick * 100);
    }
    CHECK_EQ(core.posted() - before, 9u);
    CHECK_EQ(app.rejectedRecords(), 0u);
}

TEST_CASE(environment_posts_outdoor_temperature_only_when_it_changes) {
    NO_HEAP_SCOPE();
    AppModel app;
    EnvironmentPollerCore core;
    EnvironmentSample sample;
    sample.outdoorValid = false;
    sample.outdoorTempC = 12.0f;
    CHECK_EQ(core.poll(sample, 0, app.mutations()), 0u);     // no reading yet
    sample.outdoorValid = true;
    CHECK_EQ(core.poll(sample, 100, app.mutations()), 1u);
    CHECK_EQ(core.poll(sample, 200, app.mutations()), 0u);
    sample.outdoorTempC = 12.7f;
    CHECK_EQ(core.poll(sample, 300, app.mutations()), 1u);
    app.drainPendingMutations(400);
    CHECK(app.environment().state().outdoorValid);
    CHECK_EQ(app.environment().state().outdoorTemp, 12.7f);
}

TEST_CASE(battery_sentinel_passes_through_to_an_invalid_reading) {
    NO_HEAP_SCOPE();
    AppModel app;
    BatteryPollerCore core;
    BatterySample sample;
    CHECK_EQ(core.poll(sample, 0, app.mutations()), 0u);     // handler not initialised: nothing
    sample.initialized = true;
    sample.percent = 83.0f;
    sample.voltage = 3.97f;
    CHECK_EQ(core.poll(sample, 0, app.mutations()), 5u);     // primed
    CHECK_EQ(core.poll(sample, 100, app.mutations()), 0u);   // steady
    app.drainPendingMutations(150);
    CHECK(app.power().state().percentValid);
    CHECK_EQ(app.power().state().percent, 83.0f);
    CHECK(!app.power().state().presenceKnown);

    sample.percent = -1.0f;                                  // the handler lost its reading
    sample.voltage = -1.0f;
    CHECK_EQ(core.poll(sample, 200, app.mutations()), 2u);
    app.drainPendingMutations(250);
    CHECK(!app.power().state().percentValid);                // invalid, not 0 %
    CHECK(!app.power().state().voltageValid);
    CHECK(app.power().state().band == BatteryBand::Invalid);

    sample.presenceKnown = true;                             // an AXP2101 that knows no cell is fitted
    sample.connected = false;
    CHECK_EQ(core.poll(sample, 300, app.mutations()), 1u);
    app.drainPendingMutations(350);
    CHECK(app.power().state().presenceKnown);
    CHECK(!app.power().state().connected);
    CHECK_EQ(app.rejectedRecords(), 2u);                     // the two sentinels are rejected by the model, by design
}
