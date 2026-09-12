// test_media_amendments (UIX-10): pendingSeekPercent -1 vs 0, seek gated on resolution and a
// known duration, the lastStateMs staleness boundary at exactly 10 s, position extrapolation,
// track.id change detection, the indoorSampledAtMs 120 s gate, zero allocations.
#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/ClimateModel.h"
#include "model/MediaModel.h"

using namespace model;

TEST_CASE(pending_seek_minus_one_is_distinct_from_zero) {
    MediaModel m;
    CHECK(m.setSeekResolution(SeekResolution::Fine));
    CHECK(m.setPosition(0, 10000, 0));
    CHECK(!m.hasPendingSeek());
    CHECK(m.setPendingSeekPercent(0));
    CHECK(m.hasPendingSeek());
    CHECK_EQ(static_cast<int>(m.state().pendingSeekPercent), 0);
    CHECK_EQ(m.takePendingSeek(), 0);
    CHECK_EQ(m.takePendingSeek(), MediaModel::kNoSeek);
    CHECK(!m.hasPendingSeek());
    CHECK(m.setPendingSeekPercent(100));
    CHECK(m.setPendingSeekPercent(MediaModel::kNoSeek));
    CHECK(!m.hasPendingSeek());
    CHECK(!m.setPendingSeekPercent(101));
}

TEST_CASE(seek_is_gated_on_resolution_and_a_known_duration) {
    MediaModel m;
    CHECK(!m.canSeek());
    CHECK_EQ(static_cast<int>(m.state().seekStepPercent), 0);
    CHECK(m.setSeekResolution(SeekResolution::Coarse));
    CHECK_EQ(static_cast<int>(m.state().seekStepPercent), 10);
    CHECK(!m.canSeek());                          // duration unknown
    CHECK(!m.setPendingSeekPercent(50));
    CHECK(!m.setKnobBinding(KnobBinding::Seek));
    CHECK(m.setPosition(0, 1000, 0));
    CHECK(m.canSeek());
    CHECK(m.setKnobBinding(KnobBinding::Seek));
    CHECK(m.setSeekResolution(SeekResolution::Fine));
    CHECK_EQ(static_cast<int>(m.state().seekStepPercent), 1);
    CHECK(m.setPosition(0, 0, 5));                // duration lost: SEEK drops back to VOLUME
    CHECK(m.state().knobBinding == KnobBinding::Volume);
    CHECK(!m.canSeek());
    CHECK(m.setPosition(0, 1000, 6));
    CHECK(m.setKnobBinding(KnobBinding::Seek));
    CHECK(m.setSeekResolution(SeekResolution::None));
    CHECK(m.state().knobBinding == KnobBinding::Volume);
}

TEST_CASE(state_staleness_boundary_is_exactly_ten_seconds) {
    MediaModel m;
    CHECK(m.isStateStale(0));                     // never seen
    CHECK(m.setPlaying(true, 1000));
    CHECK(!m.isStateStale(10999));
    CHECK(m.isStateStale(11000));
    m.noteStateSeen(20000);
    CHECK(!m.isStateStale(29999));
    CHECK(m.isStateStale(30000));
}

TEST_CASE(position_is_sampled_and_extrapolated_while_playing) {
    MediaModel m;
    CHECK_EQ(m.estimatedPositionMs(0), -1);
    CHECK(m.setPosition(2000, 10000, 100));
    CHECK_EQ(m.estimatedPositionMs(600), 2000);   // paused: no extrapolation
    CHECK(m.setPlaying(true, 100));
    CHECK_EQ(m.estimatedPositionMs(600), 2500);
    CHECK_EQ(m.estimatedPositionMs(100000), 10000);  // clamped to the duration
    CHECK(m.setPosition(12000, 10000, 200));      // over the end clamps
    CHECK_EQ(m.state().positionMs, 10000);
    CHECK(!m.setPosition(-1, 10000, 200));
}

TEST_CASE(track_id_change_is_detected) {
    MediaModel m;
    CHECK(m.setTrack("T", "A", "L", "id1"));
    CHECK(!m.setTrack("T", "A", "L", "id1"));
    CHECK(m.setTrack("T", "A", "L", "id2"));
    CHECK(m.state().track.id == FixedString<24>("id2"));
}

TEST_CASE(indoor_sample_gate_is_120_seconds) {
    ClimateModel c;
    CHECK(!c.indoorFresh(0));
    c.noteIndoorSample(1000);
    CHECK(c.indoorFresh(1000));
    CHECK(c.indoorFresh(121000));
    CHECK(!c.indoorFresh(121001));
}

TEST_CASE(amendments_allocate_nothing) {
    NO_HEAP_SCOPE();
    MediaModel m;
    ClimateModel c;
    CHECK(m.setSeekResolution(SeekResolution::Fine));
    for (int i = 0; i < 2000; ++i) {
        m.setPosition(i, 5000, i);
        m.setPendingSeekPercent(i % 101);
        (void)m.takePendingSeek();
        m.setTrack("t", "a", "l", i % 2 ? "x" : "y");
        c.noteIndoorSample(i);
        m.commit();
        c.commit();
    }
    CHECK(m.canSeek());
}
