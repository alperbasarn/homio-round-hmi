// test_knob_binding_machine (MEDIA-14): in VOLUME every detent is a SetVolume; in SEEK N detents
// produce one SeekTo and zero SetVolume, the release that ends the scrub is consumed, an idle
// scrub commits after 450 ms, an idle SEEK reverts after 6 s, the coarse PC path snaps to the
// 10 % grid, seek cannot be entered without a duration, back discards, and nothing allocates.
#include "HeapGuard.h"
#include "KnobBindingMachine.h"
#include "TestRunner.h"

using controllers::KnobAction;
using controllers::KnobBindingMachine;
using controllers::KnobBindingState;
using model::MediaState;
using model::SeekResolution;

namespace {

MediaState seekable(int positionMs, int durationMs, SeekResolution res) {
    MediaState m;
    m.volume = 40;
    m.positionMs = positionMs;
    m.durationMs = durationMs;
    m.seekResolution = res;
    m.seekStepPercent = res == SeekResolution::Coarse ? 10 : (res == SeekResolution::Fine ? 1 : 0);
    return m;
}

}  // namespace

TEST_CASE(volume_binding_turns_every_detent_into_a_clamped_set_volume) {
    KnobBindingMachine km;
    NO_HEAP_SCOPE();
    MediaState m = seekable(0, 0, SeekResolution::None);
    KnobAction a = km.onDetents(3, m, 100);
    CHECK(a.kind == KnobAction::Kind::SetVolume);
    CHECK_EQ(a.value, 43);
    m.volume = 99;
    CHECK_EQ(km.onDetents(5, m, 110).value, 100);
    m.volume = 1;
    CHECK_EQ(km.onDetents(-5, m, 120).value, 0);
    CHECK(km.onDetents(0, m, 130).kind == KnobAction::Kind::None);
    CHECK(km.enterSeek(m, 140).kind == KnobAction::Kind::None);  // no duration: no seek
    CHECK(!km.seeking());
    KnobAction out;
    CHECK(!km.onRelease(out, 150));  // a tap in VOLUME is the caller's
}

TEST_CASE(n_detents_in_seek_make_one_seek_to_and_the_terminating_release_is_consumed) {
    KnobBindingMachine km;
    const MediaState m = seekable(30000, 100000, SeekResolution::Fine);  // 30 %
    KnobAction a = km.enterSeek(m, 1000);
    CHECK(a.kind == KnobAction::Kind::BindingChanged && a.value == 1);
    CHECK(km.state() == KnobBindingState::SeekIdle);
    int setVolume = 0;
    int seekTo = 0;
    for (int i = 0; i < 12; ++i) {
        a = km.onDetents(1, m, 1010 + i * 10);
        if (a.kind == KnobAction::Kind::SetVolume) ++setVolume;
        if (a.kind == KnobAction::Kind::SeekTo) ++seekTo;
        CHECK(a.kind == KnobAction::Kind::Scrubbing);
    }
    CHECK_EQ(setVolume, 0);
    CHECK_EQ(seekTo, 0);
    CHECK_EQ(km.scrubPercent(), 42);
    KnobAction out;
    CHECK(km.onRelease(out, 1200));  // consumed: not a play/pause tap
    CHECK(out.kind == KnobAction::Kind::SeekTo);
    CHECK_EQ(out.value, 42);
    CHECK(km.state() == KnobBindingState::SeekIdle);
    CHECK_EQ(km.scrubPercent(), -1);
    CHECK(!km.onRelease(out, 1300));  // the next tap is a tap again
}

TEST_CASE(an_idle_scrub_commits_after_450_ms_and_an_idle_seek_reverts_after_6_s) {
    KnobBindingMachine km;
    const MediaState m = seekable(50000, 100000, SeekResolution::Fine);
    km.enterSeek(m, 1000);
    km.onDetents(-3, m, 1100);
    CHECK(km.tick(1500).kind == KnobAction::Kind::None);   // 400 ms idle: still scrubbing
    KnobAction a = km.tick(1560);
    CHECK(a.kind == KnobAction::Kind::SeekTo);
    CHECK_EQ(a.value, 47);
    CHECK(km.tick(7000).kind == KnobAction::Kind::None);   // 5440 ms since the commit
    a = km.tick(7600);
    CHECK(a.kind == KnobAction::Kind::BindingChanged && a.value == 0);
    CHECK(!km.seeking());
}

TEST_CASE(the_coarse_pc_path_snaps_to_the_ten_percent_grid_and_back_discards) {
    KnobBindingMachine km;
    const MediaState m = seekable(37000, 100000, SeekResolution::Coarse);  // 37 % -> grid 30
    km.enterSeek(m, 1000);
    KnobAction a = km.onDetents(1, m, 1010);
    CHECK(a.kind == KnobAction::Kind::Scrubbing);
    CHECK_EQ(a.value, 40);
    a = km.onDetents(2, m, 1020);
    CHECK_EQ(a.value, 60);
    a = km.onDetents(9, m, 1030);
    CHECK_EQ(a.value, 100);
    a = km.exitSeek(1040);  // back: nothing committed
    CHECK(a.kind == KnobAction::Kind::BindingChanged && a.value == 0);
    CHECK_EQ(km.scrubPercent(), -1);
    CHECK(km.tick(2000).kind == KnobAction::Kind::None);
    CHECK(km.onDetents(1, m, 2100).kind == KnobAction::Kind::SetVolume);
}
