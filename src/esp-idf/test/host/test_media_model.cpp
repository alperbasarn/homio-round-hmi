// test_media_model (MDL-15): clamps, out-of-range rejection (an empty payload must not mute),
// echo suppression, publish rate limit, truncation, target switch, the two availability slots
// independent, and zero allocations on the notify path.
#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/MediaModel.h"

using namespace model;

namespace {

void countNotify(const MediaState&, void* context) { ++*static_cast<int*>(context); }

}  // namespace

TEST_CASE(defaults_match_the_sound_controller) {
    MediaModel m;
    CHECK_EQ(m.state().volume, 50);
    CHECK(!m.state().isPlaying);
    CHECK(!m.state().stateSynced);
    CHECK(m.state().activeTarget == MediaTargetId::Pc);
    CHECK(m.state().knobBinding == KnobBinding::Volume);
    CHECK(m.availabilityOf(MediaTargetId::Spotify) == MediaAvailability::NoToken);
    CHECK(m.availabilityOf(MediaTargetId::Pc) == MediaAvailability::PcOffline);
    CHECK(!m.activeTargetAvailable());
    CHECK(!m.hasPendingSeek());
}

TEST_CASE(volume_accepts_0_to_100_and_rejects_everything_else) {
    MediaModel m;
    CHECK(!m.setVolume(101));
    CHECK(!m.setVolume(-1));  // what an empty payload decodes to: it used to mute the device
    CHECK_EQ(m.state().volume, 50);
    CHECK(m.setVolume(0));
    CHECK_EQ(m.state().volume, 0);
    CHECK(m.setVolume(100));
    CHECK_EQ(m.state().volume, 100);
    CHECK(!m.setVolumeFromRemote(200, 0));
    CHECK_EQ(m.state().volume, 100);
}

TEST_CASE(an_equal_set_notifies_nobody) {
    MediaModel m;
    int notified = 0;
    CHECK(m.subscribe(&countNotify, &notified));
    CHECK(m.setVolume(50));      // the current value
    CHECK(!m.commit());
    CHECK_EQ(notified, 0);
    CHECK(m.setVolume(60));
    CHECK(m.commit());
    CHECK_EQ(notified, 1);
    CHECK(m.setVolume(60));
    CHECK(!m.commit());
    CHECK_EQ(notified, 1);
    CHECK_EQ(m.readSnapshot().volume, 60);
}

TEST_CASE(echo_of_our_own_publish_is_ignored_inside_the_window) {
    MediaModel m;
    CHECK(m.setVolume(70));
    m.markVolumeChanged();
    CHECK(m.shouldPublishVolume(0));            // records 70 as published at t = 0
    CHECK(!m.setVolumeFromRemote(70, 100));     // the broker echoes it back
    CHECK(m.setVolumeFromRemote(71, 100));      // a different value is a real change
    CHECK_EQ(m.state().volume, 71);
    CHECK(!m.setVolumeFromRemote(70, 1500));    // boundary inclusive
    CHECK(m.setVolumeFromRemote(70, 1501));     // expired
    CHECK_EQ(m.state().volume, 70);
}

TEST_CASE(volume_publishes_at_most_once_per_window_and_flushes_the_last_value) {
    MediaModel m;
    m.markVolumeChanged();
    CHECK(m.shouldPublishVolume(0));
    m.markVolumeChanged();
    CHECK(!m.shouldPublishVolume(5));
    CHECK(!m.shouldPublishVolume(24));
    CHECK(m.shouldPublishVolume(25));
    CHECK(!m.flushVolumePublish(26));           // nothing pending
    m.markVolumeChanged();
    CHECK(m.volumePublishPending());
    CHECK(m.flushVolumePublish(27));            // leave-screen flush ignores the window
    CHECK(!m.volumePublishPending());
}

TEST_CASE(track_strings_truncate_to_their_capacity) {
    MediaModel m;
    char longTitle[128];
    for (int i = 0; i < 127; ++i) longTitle[i] = 'a' + (i % 26);
    longTitle[127] = '\0';
    CHECK(m.setTrack(longTitle, "artist", "album", "0123456789012345678901234567890"));
    CHECK_EQ(m.state().track.title.size(), 64u);
    CHECK_EQ(m.state().track.id.size(), 24u);
    CHECK(!m.setTrack(longTitle, "artist", "album", "0123456789012345678901234567890"));  // unchanged
    CHECK(m.setTargetLabel("Spotify - Firefox window title that is long"));
    CHECK_EQ(m.state().targetLabel.size(), 24u);
}

TEST_CASE(the_two_availability_slots_are_independent) {
    MediaModel m;
    CHECK(m.setAvailability(MediaTargetId::Spotify, MediaAvailability::NoToken));
    CHECK(m.setAvailability(MediaTargetId::Pc, MediaAvailability::PcOffline));
    CHECK(m.availabilityOf(MediaTargetId::Spotify) == MediaAvailability::NoToken);
    CHECK(m.availabilityOf(MediaTargetId::Pc) == MediaAvailability::PcOffline);
    CHECK(m.setAvailability(MediaTargetId::Pc, MediaAvailability::Ok));
    CHECK(m.availabilityOf(MediaTargetId::Spotify) == MediaAvailability::NoToken);  // untouched
    CHECK(m.activeTargetAvailable());
    CHECK(!m.setAvailability(MediaTargetId::Spotify, MediaAvailability::PcOffline));  // wrong slot
    CHECK(!m.setAvailability(MediaTargetId::Pc, MediaAvailability::NoPremium));
    CHECK(!m.setAvailability(MediaTargetId::Count, MediaAvailability::Ok));
    CHECK(m.availabilityOf(MediaTargetId::Count) == MediaAvailability::Count);
}

TEST_CASE(switching_target_forgets_what_was_learned_about_the_old_one) {
    MediaModel m;
    CHECK(m.setPlaying(true, 10));
    CHECK(m.state().stateSynced);
    CHECK(m.setPosition(1000, 5000, 10));
    CHECK(m.setTrack("A", "B", "C", "id"));
    CHECK(m.setActiveTarget(MediaTargetId::Spotify));
    CHECK(m.state().activeTarget == MediaTargetId::Spotify);
    CHECK(!m.state().isPlaying);
    CHECK(!m.state().stateSynced);
    CHECK_EQ(m.state().durationMs, 0);
    CHECK(m.state().track.title.empty());
    CHECK_EQ(m.state().lastStateMs, static_cast<Millis>(-1));
    CHECK(m.isStateStale(10));
    CHECK(!m.setActiveTarget(MediaTargetId::Count));
    CHECK(m.setActiveTarget(MediaTargetId::Spotify));  // same target: nothing forgotten
}

TEST_CASE(media_model_allocates_nothing_on_the_notify_path) {
    NO_HEAP_SCOPE();
    MediaModel m;
    int notified = 0;
    CHECK(m.subscribe(&countNotify, &notified));
    for (int i = 0; i < 1000; ++i) {
        m.setVolume(i % 101);
        m.setTrack(i % 2 ? "one" : "two", "artist", "album", "id");
        m.setPlaying(i % 3 == 0, i);
        m.commit();
    }
    CHECK(notified > 0);
}
