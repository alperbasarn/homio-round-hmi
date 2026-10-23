// test_connectivity_model (MDL-23): the four flags independent, strength 0/4 accepted and 5
// rejected, five distinct bar values from RSSI, zero allocations.
#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/ConnectivityModel.h"

using namespace model;

namespace {

void countNotify(const ConnectivityState&, void* context) { ++*static_cast<int*>(context); }

}  // namespace

TEST_CASE(the_four_flags_are_independent) {
    ConnectivityModel c;
    CHECK(c.setWifiConnected(true));
    CHECK(c.setInternetAvailable(true));
    CHECK(c.setMqttConnected(true));
    CHECK(c.setMqttLightConfigured(true));
    CHECK(c.setMqttConnected(false));
    CHECK(c.state().wifiConnected);           // mqtt=false does not clear wifi
    CHECK(c.state().internetAvailable);
    CHECK(c.state().mqttLightConfigured);
    CHECK(c.setWifiConnected(false));
    CHECK(c.state().internetAvailable);       // the poller reports each flag; the model cascades nothing
    CHECK(!c.state().mqttConnected);
}

TEST_CASE(strength_0_to_4_accepted_and_5_rejected) {
    ConnectivityModel c;
    for (int s = 0; s <= 4; ++s) {
        CHECK(c.setSignalStrength(s));
        CHECK_EQ(static_cast<int>(c.state().bars), s);
    }
    CHECK(!c.setSignalStrength(5));
    CHECK(!c.setSignalStrength(-1));
    CHECK_EQ(static_cast<int>(c.state().bars), 4);
}

TEST_CASE(five_distinct_inputs_give_five_distinct_bar_values) {
    ConnectivityModel c;
    const int rssi[5] = {-30, -55, -65, -75, -90};
    int bars[5];
    for (int i = 0; i < 5; ++i) {
        CHECK(c.setRssi(rssi[i]));
        bars[i] = c.state().bars;
        CHECK_EQ(c.state().rssiDbm, rssi[i]);
    }
    CHECK_EQ(bars[0], 4);
    CHECK_EQ(bars[1], 3);
    CHECK_EQ(bars[2], 2);
    CHECK_EQ(bars[3], 1);
    CHECK_EQ(bars[4], 0);
    CHECK(!c.setRssi(-200));
    CHECK(!c.setRssi(50));
}

TEST_CASE(connectivity_model_allocates_nothing) {
    NO_HEAP_SCOPE();
    ConnectivityModel c;
    int notified = 0;
    CHECK(c.subscribe(&countNotify, &notified));
    for (int i = 0; i < 10000; ++i) {
        c.setWifiConnected(i % 2 == 0);
        c.setRssi(-30 - (i % 70));
        c.commit();
    }
    CHECK(notified > 0);
}
