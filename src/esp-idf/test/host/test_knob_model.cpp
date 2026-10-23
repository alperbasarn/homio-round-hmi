// test_knob_model (MDL-29): defaults equal the knob firmware, net-zero deltas, burst -> delta
// (+3 from 40 to 43, clamped at +-10), absolute never stored as a mode value,
// mode_switch_does_not_leak_setpoint (fails against today's dispatch, which writes the counter
// into whichever mode is active), the write timeout, clamps at both ends, link timeout, the
// per-divisor accumulator with its reversal and idle resets, haptic validation, zero
// allocations.
#include <cmath>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/KnobModel.h"

using namespace model;

namespace {

void countNotify(const KnobState&, void* context) { ++*static_cast<int*>(context); }

}  // namespace

TEST_CASE(defaults_equal_the_knob_firmware) {
    KnobModel k;
    CHECK_EQ(k.state().haptic.poles, 120);
    CHECK_EQ(k.state().haptic.detentStrength, 1.0f);
    CHECK_EQ(k.state().haptic.snapPoint, 0.55f);
    CHECK_EQ(k.state().haptic.voltageLimit, 3.0f);
    CHECK_EQ(k.state().detentCount, 0);
    CHECK(!k.state().linkAlive);
    CHECK(!k.hasPendingWrite());
}

TEST_CASE(net_zero_deltas_leave_the_replica_where_it_was) {
    KnobModel k;
    CHECK_EQ(k.applyDelta(5, 0), 5);
    CHECK_EQ(k.state().detentCount, 5);
    CHECK_EQ(k.applyDelta(-5, 10), -5);
    CHECK_EQ(k.state().detentCount, 0);
    CHECK_EQ(k.applyDelta(0, 20), 0);
}

TEST_CASE(burst_from_40_to_43_is_plus_3) {
    KnobModel k;
    CHECK_EQ(k.applyDelta(40, 0), 40);
    CHECK_EQ(k.burstToDelta(43, 10), 3);
    CHECK_EQ(k.state().detentCount, 43);
    CHECK_EQ(k.state().clampedBursts, 0u);
}

TEST_CASE(burst_clamps_to_plus_minus_10_and_never_yields_a_value) {
    KnobModel k;
    CHECK_EQ(k.applyDelta(43, 0), 43);
    CHECK_EQ(k.burstToDelta(90, 20), 10);        // the screen moves 10, never to 90
    CHECK_EQ(k.state().clampedBursts, 1u);
    CHECK_EQ(k.state().detentCount, 90);         // the replica records what the knob said
    CHECK_EQ(k.burstToDelta(0, 30), -10);
    CHECK_EQ(k.burstToDelta(250, 40), 10);       // out-of-range bursts clamp to the counter range first
    CHECK_EQ(k.state().detentCount, 100);
}

TEST_CASE(mode_switch_does_not_leak_setpoint) {
    KnobModel k;
    CHECK_EQ(k.applyDelta(80, 0), 80);           // MEDIA: the knob counter sits at 80
    k.onEnter(KnobContext::Volume, 20, 100);     // enter LIGHT at brightness 20: push 20 down
    CHECK(k.hasPendingWrite());
    CHECK_EQ(k.burstToDelta(80, 110), 0);        // a stale MEDIA-era burst arrives: dropped
    CHECK_EQ(k.state().droppedBursts, 1u);
    CHECK_EQ(k.takeWriteRequest(120), 20);
    CHECK_EQ(k.takeWriteRequest(121), -1);       // already sent
    CHECK(k.noteWriteAck(20));
    CHECK_EQ(k.state().detentCount, 20);
    CHECK(!k.hasPendingWrite());
    CHECK_EQ(k.burstToDelta(23, 130), 3);        // the next burst is relative to the pushed value
    CHECK(!k.noteWriteAck(20));                  // nothing pending
}

TEST_CASE(write_timeout_assumes_the_write_landed) {
    KnobModel k;
    CHECK(k.requestWrite(35, 0));
    CHECK_EQ(k.takeWriteRequest(0), 35);
    k.tick(1000);
    CHECK(k.hasPendingWrite());
    k.tick(1001);
    CHECK(!k.hasPendingWrite());
    CHECK_EQ(k.state().detentCount, 35);
    CHECK_EQ(k.state().writeTimeouts, 1u);
    CHECK(k.requestWrite(-5, 2000));             // clamps
    CHECK_EQ(k.takeWriteRequest(2000), 0);
}

TEST_CASE(clamp_at_both_ends) {
    KnobModel k;
    k.applyDelta(-3, 0);
    CHECK_EQ(k.state().detentCount, 0);
    k.applyDelta(100, 10);
    k.applyDelta(3, 20);
    CHECK_EQ(k.state().detentCount, 100);
}

TEST_CASE(link_timeout) {
    KnobModel k;
    CHECK(!k.linkAlive(0));
    k.noteLinkActivity(100);
    CHECK(k.linkAlive(5100));
    CHECK(!k.linkAlive(5101));
    k.tick(5100);
    CHECK(k.state().linkAlive);
    k.tick(5101);
    CHECK(!k.state().linkAlive);
    k.applyDelta(1, 6000);                       // any line revives it
    CHECK(k.state().linkAlive);
}

TEST_CASE(per_divisor_accumulator_with_reversal_and_idle_resets) {
    KnobModel k;
    k.setContext(KnobContext::Wheel);             // 4 detents per step
    CHECK_EQ(k.stepsFor(1, 0), 0);
    CHECK_EQ(k.stepsFor(1, 10), 0);
    CHECK_EQ(k.stepsFor(1, 20), 0);
    CHECK_EQ(k.stepsFor(1, 30), 1);
    CHECK_EQ(k.stepsFor(2, 40), 0);              // accumulator 2
    CHECK_EQ(k.stepsFor(-1, 50), 0);             // reversal: reset, then -1
    CHECK_EQ(k.state().accumulator, -1);
    CHECK_EQ(k.stepsFor(-3, 60), -1);            // -4 -> one step back
    CHECK_EQ(k.stepsFor(-3, 70), 0);             // accumulator -3
    CHECK_EQ(k.stepsFor(-1, 671), 0);            // 601 ms idle: reset, then -1
    CHECK_EQ(k.state().accumulator, -1);
    k.setContext(KnobContext::TargetToggle);      // 3 per toggle; setContext resets
    CHECK_EQ(k.state().accumulator, 0);
    CHECK_EQ(k.stepsFor(3, 700), 1);
    k.setContext(KnobContext::SeekCoarse);        // 3 per percent
    CHECK_EQ(k.stepsFor(7, 800), 2);
    CHECK_EQ(k.state().accumulator, 1);
    k.setContext(KnobContext::Volume);            // 1 per unit
    CHECK_EQ(k.stepsFor(5, 900), 5);
    k.setContext(KnobContext::ClimateHalfDegree);
    CHECK_EQ(k.stepsFor(-2, 1000), -2);
    k.setContext(KnobContext::SeekFine);
    CHECK_EQ(k.stepsFor(1, 1100), 1);
    CHECK_EQ(k.applyDelta(8, 1200), 8);          // applyDelta feeds the same accumulator
}

TEST_CASE(haptic_config_is_validated) {
    KnobModel k;
    CHECK(!k.setPoles(0));
    CHECK(!k.setSnapPoint(0.4f));
    CHECK(!k.setDetentStrength(std::nanf("")));
    CHECK(!k.setVoltageLimit(20.0f));
    HapticConfig config;
    config.poles = 240;
    config.detentStrength = 0.8f;
    config.snapPoint = 0.6f;
    config.voltageLimit = 2.5f;
    CHECK(k.setHaptic(config));
    CHECK(k.state().haptic == config);
    CHECK(k.state().haptic != HapticConfig{});
}

TEST_CASE(knob_model_allocates_nothing) {
    NO_HEAP_SCOPE();
    KnobModel k;
    int notified = 0;
    CHECK(k.subscribe(&countNotify, &notified));
    for (int i = 0; i < 10000; ++i) {
        k.applyDelta(i % 3 - 1, i);
        if (i % 50 == 0) k.burstToDelta(i % 101, i);
        if (i % 500 == 0) k.onEnter(KnobContext::Volume, i % 101, i);
        if (i % 500 == 1) (void)k.takeWriteRequest(i);
        if (i % 500 == 2) k.noteWriteAck((i - 2) % 101);
        k.tick(i);
        k.commit();
    }
    CHECK(notified > 0);
}
