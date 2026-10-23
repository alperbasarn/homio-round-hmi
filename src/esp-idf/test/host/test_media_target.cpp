// test_media_target (MEDIA-02/03/04/07/08/09/10): the registry answers NotAvailable for an
// unregistered slot without a crash or an allocation; the PC adapter publishes the v1 bodies,
// coalesces a detent burst to at most three publishes with the last one final, drops the echo of
// its own setpoint, gates play/pause on the known state (the PC maps both onto one toggling media
// key), learns v2 from a reply and only then seeks, and lands replies in the model.
#include <cstring>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "media_target/MediaTargetRegistry.h"
#include "media_target/PcMediaAdapter.h"
#include "model/AppModel.h"

using namespace media;
using namespace model;

namespace {

struct Recorder {
    static constexpr int kMax = 64;
    char topics[kMax][32];
    char bodies[kMax][64];
    int count = 0;
    bool refuse = false;
    static bool publish(const char* topic, const char* body, void* context) {
        Recorder* r = static_cast<Recorder*>(context);
        if (r->refuse) return false;
        if (r->count < kMax) {
            std::strncpy(r->topics[r->count], topic, 31);
            r->topics[r->count][31] = 0;
            std::strncpy(r->bodies[r->count], body, 63);
            r->bodies[r->count][63] = 0;
        }
        ++r->count;
        return true;
    }
    const char* last() const { return count > 0 ? bodies[(count - 1) % kMax] : ""; }
    bool lastIs(const char* topic, const char* body) const {
        return count > 0 && std::strcmp(topics[(count - 1) % kMax], topic) == 0 && std::strcmp(last(), body) == 0;
    }
};

struct Rig {
    AppModel app;
    Recorder rec;
    PcMediaAdapter pc{&app.mutations(), &app.media(), &Recorder::publish, &rec};
    MediaTargetRegistry registry;
    int volume() { return app.media().state().volume; }
};

}  // namespace

TEST_CASE(an_unregistered_slot_answers_not_available_without_a_crash) {
    MediaTargetRegistry reg;
    NO_HEAP_SCOPE();
    IMediaTarget& spotify = reg.target(MediaTargetId::Spotify);
    CHECK(!reg.registered(MediaTargetId::Spotify));
    CHECK(spotify.id() == MediaTargetId::Spotify);
    CHECK(!spotify.available());
    CHECK(spotify.setVolume(50, 0) == MediaError::NotAvailable);
    CHECK(spotify.play(0) == MediaError::NotAvailable);
    CHECK(spotify.seekAbsolute(10, 0) == MediaError::NotAvailable);
    CHECK(!spotify.caps().volume);
    CHECK(reg.target(MediaTargetId::Count).id() == MediaTargetId::Count);
    CHECK(!reg.unregisterTarget(MediaTargetId::Pc));
    reg.tickAll(100);
}

TEST_CASE(the_registry_holds_one_target_per_slot) {
    Rig r;
    CHECK(r.registry.registerTarget(&r.pc));
    CHECK(!r.registry.registerTarget(&r.pc));  // the slot is taken
    CHECK(!r.registry.registerTarget(nullptr));
    CHECK(r.registry.registered(MediaTargetId::Pc));
    CHECK(&r.registry.target(MediaTargetId::Pc) == &r.pc);
    CHECK(r.registry.unregisterTarget(MediaTargetId::Pc));
    CHECK(!r.registry.registered(MediaTargetId::Pc));
    CHECK(r.registry.target(MediaTargetId::Pc).setVolume(1, 0) == MediaError::NotAvailable);
}

TEST_CASE(the_pc_adapter_publishes_the_v1_bodies_only_while_the_link_is_up) {
    Rig r;
    CHECK(r.pc.setVolume(30, 0) == MediaError::NotAvailable);
    CHECK(r.pc.play(0) == MediaError::NotAvailable);
    CHECK_EQ(r.rec.count, 0);
    r.pc.setLinkUp(true, 50);
    CHECK(r.rec.lastIs("esp32/sound/get_state", "request"));  // link-up asks for the state
    CHECK(r.pc.setVolume(30, 100) == MediaError::Ok);
    CHECK(r.rec.lastIs("esp32/sound/setpoint", "setpoint:30"));
    CHECK(r.pc.play(200) == MediaError::Ok);
    CHECK(r.rec.lastIs("esp32/sound/control", "play"));
    CHECK(r.pc.pause(300) == MediaError::Ok);
    CHECK(r.rec.lastIs("esp32/sound/control", "pause"));
    CHECK(r.pc.requestState(400) == MediaError::Ok);
    CHECK_EQ(r.rec.count, 5);
    CHECK(r.pc.setVolume(101, 500) == MediaError::InvalidArgument);
    r.rec.refuse = true;
    CHECK(r.pc.play(600) == MediaError::Transport);
    CHECK_EQ(r.pc.publishFailures(), 1u);
}

TEST_CASE(a_detent_burst_coalesces_to_at_most_three_publishes_with_the_last_one_final) {
    Rig r;
    r.pc.setLinkUp(true, 0);
    const int before = r.rec.count;
    int volume = 0;
    for (Millis t = 1000; t < 1200; t += 2) {  // 100 detents in 200 ms
        CHECK(r.pc.setVolume(++volume, t) == MediaError::Ok);
        r.pc.tick(t);
    }
    for (Millis t = 1200; t <= 1600; t += 16) {  // the knob rests
        r.pc.tick(t);
    }
    CHECK(r.rec.count - before <= 3);
    CHECK(r.rec.count - before >= 2);
    CHECK(r.rec.lastIs("esp32/sound/setpoint", "setpoint:100"));
    CHECK(!r.pc.volumePending());
    CHECK(r.pc.volumeInFlight());
}

TEST_CASE(a_failed_setpoint_publish_is_retried_until_it_goes_out) {
    Rig r;
    r.pc.setLinkUp(true, 0);
    r.rec.refuse = true;
    CHECK(r.pc.setVolume(70, 100) == MediaError::Ok);
    CHECK(r.pc.volumePending());
    r.pc.tick(230);
    CHECK(r.pc.volumePending());
    r.rec.refuse = false;
    r.pc.tick(360);
    CHECK(!r.pc.volumePending());
    CHECK(r.rec.lastIs("esp32/sound/setpoint", "setpoint:70"));
}

TEST_CASE(the_echo_of_our_own_setpoint_never_lands_in_the_model) {
    Rig r;
    r.pc.setLinkUp(true, 0);
    CHECK(r.pc.setVolume(40, 100) == MediaError::Ok);
    CHECK(r.pc.volumeInFlight());
    CHECK(r.pc.onResponse("sender=pc-1,setpoint:40", 150));  // the PC reports what we sent
    r.app.drainPendingMutations(150);
    CHECK_EQ(r.volume(), 50);  // the model default: nothing landed
    CHECK(!r.pc.volumeInFlight());
    CHECK_EQ(r.pc.echoesDropped(), 1u);
    CHECK(r.pc.onResponse("state:playing,volume:33", 200));  // a real change on the PC
    r.app.drainPendingMutations(200);
    CHECK_EQ(r.volume(), 33);
    CHECK(r.pc.setVolume(60, 300) == MediaError::Ok);
    CHECK(r.pc.onResponse("setpoint:33", 350));  // stale: the PC had not applied 60 yet
    CHECK(r.pc.onResponse("setpoint:60", 400));  // the acknowledgement
    r.app.drainPendingMutations(400);
    CHECK_EQ(r.volume(), 33);
    CHECK(!r.pc.volumeInFlight());
    // A lost publish: the window expires and the PC's value is believed again.
    CHECK(r.pc.setVolume(80, 1000) == MediaError::Ok);
    CHECK(r.pc.onResponse("setpoint:12", 1500));
    r.app.drainPendingMutations(1500);
    CHECK_EQ(r.volume(), 33);
    CHECK(r.pc.onResponse("setpoint:12", 2600));
    r.app.drainPendingMutations(2600);
    CHECK_EQ(r.volume(), 12);
}

TEST_CASE(two_taps_start_then_stop_and_never_double_toggle) {
    Rig r;
    r.pc.setLinkUp(true, 0);
    CHECK(r.pc.onResponse("paused", 100));
    const int before = r.rec.count;
    CHECK(r.pc.play(200) == MediaError::Ok);
    CHECK(r.rec.lastIs("esp32/sound/control", "play"));
    CHECK(r.pc.play(250) == MediaError::Ok);  // idempotent: the key would pause again
    CHECK_EQ(r.rec.count - before, 1);
    CHECK(r.pc.pause(300) == MediaError::Ok);
    CHECK(r.rec.lastIs("esp32/sound/control", "pause"));
    CHECK(r.pc.toggle(350) == MediaError::Busy);  // suppressed until the reply or 600 ms
    CHECK(r.pc.onResponse("paused", 400));
    CHECK(r.pc.toggle(450) == MediaError::Ok);   // v1: the opposite of the known state
    CHECK(r.rec.lastIs("esp32/sound/control", "play"));
    CHECK(r.pc.toggle(500) == MediaError::Busy);
    CHECK(r.pc.toggle(1100) == MediaError::Ok);  // the suppression expired without a reply
    CHECK(r.rec.lastIs("esp32/sound/control", "pause"));
    CHECK_EQ(r.rec.count - before, 4);
}

TEST_CASE(replies_land_in_the_model_and_v2_unlocks_seek) {
    Rig r;
    r.pc.setLinkUp(true, 0);
    CHECK(r.pc.seekAbsolute(10, 0) == MediaError::NotSupported);  // default closed
    CHECK(r.pc.seekRelative(-5000, 0) == MediaError::NotSupported);
    CHECK(!r.pc.available());
    CHECK(r.pc.onResponse("sender=pc-1,state:playing,volume:20", 1000));
    r.app.drainPendingMutations(1000);
    CHECK(r.app.media().state().isPlaying);
    CHECK_EQ(r.volume(), 20);
    CHECK(r.app.media().state().stateSynced);
    CHECK(r.app.media().availabilityOf(MediaTargetId::Pc) == MediaAvailability::Ok);
    CHECK(r.pc.available());
    CHECK(!r.pc.v2Seen());
    CHECK_EQ(r.app.media().state().durationMs, 0);  // v1: the position bar stays hidden
    CHECK(r.pc.onResponse("state:paused,volume:21,position:5000,duration:60000,title:Hey, Jude,player:Spotify", 2000));
    r.app.drainPendingMutations(2000);
    CHECK(r.pc.v2Seen());
    CHECK(r.pc.caps().seekAbsolute && r.pc.caps().seekStepPercent == 10);
    CHECK_EQ(r.app.media().state().positionMs, 5000);
    CHECK_EQ(r.app.media().state().durationMs, 60000);
    CHECK(r.app.media().state().seekResolution == SeekResolution::Coarse);
    CHECK(std::strcmp(r.app.media().state().track.title.c_str(), "Hey, Jude") == 0);
    CHECK(std::strcmp(r.pc.label(), "Spotify") == 0);
    CHECK(std::strcmp(r.app.media().state().targetLabel.c_str(), "Spotify") == 0);
    CHECK(r.pc.seekAbsolute(37, 2100) == MediaError::Ok);
    CHECK(r.rec.lastIs("esp32/sound/control", "seek_abs:37"));
    CHECK(r.pc.seekRelative(-5000, 2150) == MediaError::Ok);
    CHECK(r.rec.lastIs("esp32/sound/control", "seek_rel:-5000"));
    CHECK(r.pc.toggle(2200) == MediaError::Ok);
    CHECK(r.rec.lastIs("esp32/sound/control", "toggle"));
    CHECK(!r.pc.onResponse("", 2300));
    r.pc.setLinkUp(false, 2400);
    r.app.drainPendingMutations(2400);
    CHECK(!r.pc.available());
    CHECK(!r.app.media().state().stateSynced);
    CHECK(r.app.media().availabilityOf(MediaTargetId::Pc) == MediaAvailability::NoMqtt);
}

TEST_CASE(the_media_path_allocates_nothing) {
    Rig r;
    r.pc.setLinkUp(true, 0);
    NO_HEAP_SCOPE();
    for (int i = 0; i < 500; ++i) {
        r.pc.setVolume(i % 101, i * 10);
        r.pc.tick(i * 10);
        if (i % 7 == 0) r.pc.onResponse("state:playing,volume:40,position:1,duration:2,title:T,player:P", i * 10);
        r.app.drainPendingMutations(i * 10);
    }
    CHECK(r.rec.count > 0);
}
