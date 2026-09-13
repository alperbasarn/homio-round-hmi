// test_led_registry_model (LED-01): eight slots and the ninth refused, presence from status and
// from the 120 s timeout, selection rules, per-slot record keys that never coalesce across strips,
// the light state following the selected strip through the aggregate root, and no heap anywhere.
#include <cstdio>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/AppModel.h"
#include "model/LedRegistryModel.h"

using namespace model;

namespace {

LedStripState tuple(bool power, int brightness, uint32_t rgb, LedEffect effect) {
    LedStripState s;
    s.power = power;
    s.brightness = static_cast<Percent>(brightness);
    s.rgb = rgb;
    s.effect = effect;
    s.count = 60;
    return s;
}

}  // namespace

TEST_CASE(eight_strips_announce_and_the_ninth_is_refused_once) {
    LedRegistryModel r;
    NO_HEAP_SCOPE();
    char id[16];
    for (int i = 0; i < 8; ++i) {
        std::snprintf(id, sizeof(id), "led-%06x", 0xa1b2c0 + i);
        CHECK_EQ(r.announce(id, "Strip", 1000 + i), i);
    }
    CHECK_EQ(r.usedCount(), static_cast<size_t>(8));
    CHECK_EQ(r.announce("led-ffffff", "Ninth", 2000), -1);
    CHECK_EQ(r.refusedAnnounces(), 1u);
    CHECK_EQ(r.announce("led-a1b2c3", "Kitchen", 3000), 3);  // known: same slot, name refreshed
    CHECK(r.slot(3)->name == FixedString<24>("Kitchen"));
    CHECK_EQ(r.slot(3)->lastSeenMs, 3000);
    CHECK_EQ(r.announce("", "x", 0), -1);
    CHECK_EQ(r.refusedAnnounces(), 1u);  // an empty id is not a refusal
    CHECK(r.forget(3));
    CHECK_EQ(r.usedCount(), static_cast<size_t>(7));
    CHECK_EQ(r.announce("led-ffffff", "Ninth", 4000), 3);  // the freed slot is reused
}

TEST_CASE(status_and_the_presence_timeout_drive_online) {
    AppModel app;
    LedRegistryModel& r = app.ledRegistry();
    const int s = r.announce("led-000001", "Desk", 0);
    CHECK(!r.slot(s)->online);
    CHECK(app.mutations().post(LedRegistryModel::make(LedRegistryModel::Kind::Status, s, 1, 10)));
    app.drainPendingMutations(10);
    CHECK(r.slot(s)->online);
    CHECK_EQ(r.onlineCount(), static_cast<size_t>(1));
    CHECK(app.mutations().post(LedRegistryModel::make(LedRegistryModel::Kind::Status, s, 0, 20)));  // the LWT
    app.drainPendingMutations(20);
    CHECK(!r.slot(s)->online);
    CHECK(r.setOnline(s, true, 1000));
    CHECK(r.noteSeen(s, 60000));
    app.drainPendingMutations(120000);  // 60 s since the last message: still online
    CHECK(r.slot(s)->online);
    app.drainPendingMutations(180001);  // 120 s and a bit: offline on this tick
    CHECK(!r.slot(s)->online);
    CHECK(!r.commandsAllowed(s));
    CHECK(r.setOnline(s, true, 180002));
    CHECK(r.setPaired(s, true));
    CHECK(r.commandsAllowed(s));
}

TEST_CASE(selection_needs_a_present_strip) {
    LedRegistryModel r;
    CHECK(!r.selectById("led-nobody"));
    CHECK(!r.select(2));
    CHECK_EQ(r.selectedIndex(), -1);
    const int a = r.announce("led-aaaaaa", "A", 0);
    const int b = r.announce("led-bbbbbb", "B", 0);
    CHECK(r.selectById("led-bbbbbb"));
    CHECK_EQ(r.selectedIndex(), b);
    CHECK(r.select(a));
    CHECK(r.forget(a));
    CHECK_EQ(r.selectedIndex(), -1);  // the selection followed the forgotten slot to none
    CHECK(r.select(-1));
    CHECK(!r.select(a));  // freed
}

TEST_CASE(records_carry_the_slot_and_never_coalesce_across_strips) {
    AppModel app;
    LedRegistryModel& r = app.ledRegistry();
    const int a = r.announce("led-aaaaaa", "A", 0);
    const int b = r.announce("led-bbbbbb", "B", 0);
    MutationQueue& q = app.mutations();
    CHECK(q.post(LedRegistryModel::make(LedRegistryModel::Kind::BrightnessWire, a, 255, 1)));
    CHECK(q.post(LedRegistryModel::make(LedRegistryModel::Kind::BrightnessWire, b, 127, 1)));
    CHECK(q.post(LedRegistryModel::make(LedRegistryModel::Kind::Color, b, 0x112233, 1)));
    CHECK(q.post(LedRegistryModel::make(LedRegistryModel::Kind::Effect, a, static_cast<int32_t>(LedEffect::Fire), 1)));
    CHECK(q.post(LedRegistryModel::make(LedRegistryModel::Kind::ParamsHigh, a, 9, 1, 144)));
    CHECK(q.post(LedRegistryModel::make(LedRegistryModel::Kind::Source, a, static_cast<int32_t>(LedSource::Web), 1, 77)));
    CHECK_EQ(app.drainPendingMutations(5), static_cast<size_t>(6));
    CHECK_EQ(app.rejectedRecords(), 0u);
    CHECK_EQ(r.slot(a)->state.brightness, 100);
    CHECK_EQ(r.slot(b)->state.brightness, 50);
    CHECK_EQ(r.slot(b)->state.rgb, 0x112233u);
    CHECK(r.slot(a)->state.effect == LedEffect::Fire);
    CHECK_EQ(r.slot(a)->state.params[4], 9);
    CHECK_EQ(r.slot(a)->state.count, 144);
    CHECK(r.slot(a)->lastSource == LedSource::Web);
    CHECK_EQ(r.slot(a)->lastSeq, 77u);
    CHECK(q.post(LedRegistryModel::make(LedRegistryModel::Kind::Effect, a, 99, 2)));  // no such effect
    app.drainPendingMutations(6);
    CHECK_EQ(app.rejectedRecords(), 1u);
}

TEST_CASE(the_light_state_follows_the_selected_strip) {
    AppModel app;
    LedRegistryModel& r = app.ledRegistry();
    const int a = r.announce("led-aaaaaa", "A", 0);
    CHECK(r.applyState(a, tuple(true, 80, 0xFF0000, LedEffect::Glow), LedSource::Device, 1, 0));
    app.drainPendingMutations(1);
    CHECK_EQ(app.light().state().stripSlot, -1);  // nothing selected yet
    CHECK(app.mutations().post(LedRegistryModel::make(LedRegistryModel::Kind::Select, a, a, 2)));
    app.drainPendingMutations(2);
    CHECK_EQ(app.light().state().stripSlot, a);
    CHECK_EQ(app.light().state().brightness, 80);
    CHECK_EQ(app.light().state().rgb, 0xFF0000u);
    CHECK(app.light().state().effect == LedEffect::Glow);
    CHECK_EQ(app.light().pendingFields(), 0u);  // adopting never publishes
    // A remote change on the strip reaches the light state on the next drain, silently.
    CHECK(r.applyState(a, tuple(false, 20, 0x00FF00, LedEffect::Glow), LedSource::Web, 2, 3));
    app.drainPendingMutations(3);
    CHECK(!app.light().state().power);
    CHECK_EQ(app.light().state().brightness, 20);
    CHECK_EQ(app.light().pendingFields(), 0u);
    // Deselecting keeps the last values but detaches the slot.
    CHECK(r.select(-1));
    app.drainPendingMutations(4);
    CHECK_EQ(app.light().state().stripSlot, -1);
}

TEST_CASE(registry_allocates_nothing) {
    LedRegistryModel r;
    NO_HEAP_SCOPE();
    for (int i = 0; i < 2000; ++i) {
        const int s = r.announce(i % 2 ? "led-000001" : "led-000002", "Strip", i);
        r.applyState(s, tuple(true, i % 101, static_cast<uint32_t>(i) & 0xFFFFFFu, LedEffect::Wave), LedSource::Self, i, i);
        r.setOnline(s, (i % 3) != 0, i);
        r.tick(i);
        r.commit();
    }
    CHECK_EQ(r.usedCount(), static_cast<size_t>(2));
}

TEST_CASE(pairing_intents_are_taken_once_and_the_flow_state_is_written_back) {
    LedRegistryModel r;
    const int a = r.announce("led-aaaaaa", "A", 0);
    CHECK(!r.requestPair(5));   // no such strip
    CHECK(!r.requestCode(7));   // out of range
    CHECK(r.requestPair(a));
    CHECK(r.requestCode(4));
    CHECK(r.requestUnpair(a));
    CHECK_EQ(r.takePairIntent(), a);
    CHECK_EQ(r.takePairIntent(), -1);
    CHECK_EQ(r.takeCodeIntent(), 4);
    CHECK_EQ(r.takeCodeIntent(), 0);
    CHECK_EQ(r.takeUnpairIntent(), a);
    CHECK_EQ(r.takeUnpairIntent(), -1);
    CHECK(r.setPairing(LedPairing::Requested, a));
    CHECK(r.pairing() == LedPairing::Requested);
    CHECK_EQ(r.pairingSlot(), a);
    CHECK(!r.setPairing(LedPairing::Count, a));
    CHECK(r.setPairing(LedPairing::Idle, -1));
    CHECK_EQ(r.pairingSlot(), -1);
}

TEST_CASE(the_first_strip_that_becomes_mine_is_selected_until_a_tap_says_otherwise) {
    LedRegistryModel r;
    const int a = r.announce("led-aaaaaa", "A", 0);
    const int b = r.announce("led-bbbbbb", "B", 0);
    CHECK_EQ(r.selectedIndex(), -1);
    CHECK(r.setPaired(b, true));
    CHECK_EQ(r.selectedIndex(), b);
    CHECK(r.setPaired(a, true));
    CHECK_EQ(r.selectedIndex(), b);  // an explicit choice is never overridden
    CHECK(r.select(a));
    CHECK(r.setPaired(a, false));
    CHECK_EQ(r.selectedIndex(), a);  // unpairing does not move the selection by itself
}
