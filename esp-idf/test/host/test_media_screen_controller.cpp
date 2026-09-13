// test_media_screen_controller (MEDIA-13 / MEDIA-15): entering requests the state and pushes the
// volume to the knob; with no reply the screen leaves WaitForSync after 3 s instead of wedging;
// a tap toggles through the target and a refusal is counted and flashed, never fatal; the knob
// moves the model volume and the target; a double tap enters seek only when the target can seek,
// detents then scrub without touching the volume, the release commits one seek and is not a tap;
// swipes skip; a hold backs out and discards a scrub; none of it allocates.
#include <cstring>

#include "HeapGuard.h"
#include "MediaScreenController.h"
#include "TestRunner.h"
#include "model/AppModel.h"

using namespace model;
using input::Gesture;
using input::HitZone;
using input::InputEvent;
using input::InputType;
using media::MediaError;

namespace {

class FakeTarget final : public media::IMediaTarget {
public:
    bool available_ = false;
    MediaError answer = MediaError::Ok;
    int setVolumeCalls = 0, lastVolume = -1, toggles = 0, plays = 0, pauses = 0, nexts = 0, prevs = 0;
    int seeks = 0, lastSeek = -1, requests = 0, ticks = 0;

    MediaTargetId id() const override { return MediaTargetId::Pc; }
    const char* label() const override { return "fake"; }
    media::MediaCaps caps() const override { return media::MediaCaps{}; }
    bool available() const override { return available_; }
    MediaError setVolume(int percent, Millis) override { ++setVolumeCalls; lastVolume = percent; return answer; }
    MediaError play(Millis) override { ++plays; return answer; }
    MediaError pause(Millis) override { ++pauses; return answer; }
    MediaError toggle(Millis) override { ++toggles; return answer; }
    MediaError next(Millis) override { ++nexts; return answer; }
    MediaError previous(Millis) override { ++prevs; return answer; }
    MediaError seekAbsolute(int percent, Millis) override { ++seeks; lastSeek = percent; return answer; }
    MediaError seekRelative(int32_t, Millis) override { return answer; }
    MediaError requestState(Millis) override { ++requests; return answer; }
    void tick(Millis) override { ++ticks; }
};

class RecordingView final : public ui::ScreenView<ui::MediaScreenState> {
public:
    int renders = 0;
    ui::MediaPhase lastPhase = ui::MediaPhase::WaitForSync;
    int lastScrub = -2;
    bool lastFlash = false;
    lv_obj_t* root() const override { return root_; }

protected:
    void doBuild(lv_obj_t* /*parent*/) override { root_ = reinterpret_cast<lv_obj_t*>(&marker_); }
    void doDestroy() override { root_ = nullptr; }
    void doSetVisible(bool /*visible*/) override {}
    void doRender(const ui::MediaScreenState& s) override {
        ++renders;
        lastPhase = s.phase;
        lastScrub = s.scrubPercent;
        lastFlash = s.unavailableFlash;
    }

private:
    int marker_ = 0;
    lv_obj_t* root_ = nullptr;
};

InputEvent ev(InputType type, HitZone zone) {
    InputEvent e;
    e.type = type;
    e.zone = zone;
    e.x = 233;
    e.y = zone == HitZone::Rim ? 60 : 233;
    return e;
}

InputEvent gesture(Gesture g) {
    InputEvent e;
    e.type = InputType::Gesture;
    e.gesture = g;
    return e;
}

InputEvent knob(int delta) {
    InputEvent e;
    e.type = InputType::KnobDelta;
    e.knobDelta = static_cast<int16_t>(delta);
    return e;
}

struct Rig {
    AppModel app;
    FakeTarget pc;
    media::MediaTargetRegistry reg;
    RecordingView view;
    controllers::MediaScreenController ctl{app, reg, &view, 466, 466};
    uint32_t now = 1000;
    Rig() { reg.registerTarget(&pc); }
    void frame() {
        now += 16;
        app.drainPendingMutations(now);
        ctl.tick(now);
    }
    void framesFor(uint32_t ms) {
        const uint32_t until = now + ms;
        while (now < until) frame();
    }
    void tap(HitZone zone = HitZone::Disc) {
        ctl.onInput(ev(InputType::TouchPress, zone), now);
        ctl.onInput(ev(InputType::TouchRelease, zone), now);
    }
    void makeSeekable() {
        app.mutations().post(MediaModel::make(MediaModel::Field::Position, 30000, now, 100000));
        app.mutations().post(MediaModel::make(MediaModel::Field::SeekResolution, static_cast<int32_t>(SeekResolution::Fine), now));
        frame();
    }
};

}  // namespace

TEST_CASE(entering_requests_the_state_and_sets_the_knob_context_then_times_out) {
    Rig r;
    r.ctl.onEnter(r.now);
    CHECK(r.view.built());
    CHECK_EQ(r.pc.requests, 1);
    r.frame();
    CHECK(r.app.knob().state().pendingWrite < 0);  // no push until the write path exists (WP3.6)
    CHECK(r.app.knob().state().context == KnobContext::Volume);
    CHECK(r.ctl.phase() == ui::MediaPhase::WaitForSync);
    r.framesFor(2900);
    CHECK(r.ctl.phase() == ui::MediaPhase::WaitForSync);
    r.framesFor(200);
    CHECK(r.ctl.phase() == ui::MediaPhase::Unsynced);  // the timeout the old sound controller never had
    CHECK(r.view.lastPhase == ui::MediaPhase::Unsynced);
    r.app.mutations().post(MediaModel::make(MediaModel::Field::StateSynced, 1, r.now));
    r.frame();
    CHECK(r.ctl.phase() == ui::MediaPhase::Live);
    r.app.mutations().post(MediaModel::make(MediaModel::Field::StateSynced, 0, r.now));  // the link dropped
    r.frame();
    CHECK(r.ctl.phase() == ui::MediaPhase::WaitForSync);
}

TEST_CASE(a_tap_toggles_through_the_target_and_a_refusal_is_counted_and_flashed) {
    Rig r;
    r.ctl.onEnter(r.now);
    r.pc.answer = MediaError::NotAvailable;
    r.tap();
    CHECK_EQ(r.pc.toggles, 1);
    CHECK_EQ(r.ctl.refusals(), 1u);
    r.frame();
    CHECK(r.view.lastFlash);
    r.framesFor(1300);
    CHECK(!r.view.lastFlash);
    r.pc.answer = MediaError::Ok;
    r.tap();
    CHECK_EQ(r.pc.toggles, 2);
    CHECK_EQ(r.ctl.refusals(), 1u);
    r.tap(HitZone::Rim);  // reserved for the target overlay
    CHECK_EQ(r.pc.toggles, 2);
    r.pc.answer = MediaError::Busy;  // the adapter's toggle suppression is not a refusal
    r.tap();
    CHECK_EQ(r.ctl.refusals(), 1u);
}

TEST_CASE(the_knob_moves_the_volume_in_the_model_and_on_the_target) {
    Rig r;
    r.ctl.onEnter(r.now);
    CHECK(r.ctl.onInput(knob(3), r.now));
    r.frame();
    CHECK_EQ(r.app.media().state().volume, 53);
    CHECK_EQ(r.pc.lastVolume, 53);
    CHECK(r.ctl.onInput(knob(-1), r.now));
    r.frame();
    CHECK_EQ(r.app.media().state().volume, 52);
    CHECK_EQ(r.pc.setVolumeCalls, 2);
    r.pc.answer = MediaError::NotAvailable;  // offline: the ring still moves, no flash
    CHECK(r.ctl.onInput(knob(1), r.now));
    r.frame();
    CHECK_EQ(r.app.media().state().volume, 53);
    CHECK(!r.view.lastFlash);
}

TEST_CASE(a_double_tap_enters_seek_only_when_the_target_can_seek_and_detents_make_one_seek) {
    Rig r;
    r.ctl.onEnter(r.now);
    CHECK(r.ctl.onInput(gesture(Gesture::DoubleTap), r.now));
    CHECK(!r.ctl.binding().seeking());
    CHECK_EQ(r.ctl.refusals(), 1u);  // no duration yet: the double tap explains itself
    r.makeSeekable();
    CHECK(r.ctl.onInput(gesture(Gesture::DoubleTap), r.now));
    CHECK(r.ctl.binding().seeking());
    r.frame();
    CHECK(r.app.media().state().knobBinding == KnobBinding::Seek);
    const int volumeCalls = r.pc.setVolumeCalls;
    CHECK(r.ctl.onInput(knob(5), r.now));
    r.frame();
    CHECK_EQ(r.app.media().state().pendingSeekPercent, 35);
    CHECK_EQ(r.view.lastScrub, 35);
    CHECK_EQ(r.pc.setVolumeCalls, volumeCalls);
    CHECK_EQ(r.app.media().state().volume, 50);
    r.tap();  // the release commits the scrub and is not a play/pause tap
    CHECK_EQ(r.pc.seeks, 1);
    CHECK_EQ(r.pc.lastSeek, 35);
    CHECK_EQ(r.pc.toggles, 0);
    r.frame();
    CHECK_EQ(r.app.media().state().pendingSeekPercent, -1);
    CHECK(r.app.media().state().knobBinding == KnobBinding::Seek);  // still bound until back or idle
    CHECK(r.ctl.onInput(knob(-2), r.now));
    r.frame();
    CHECK_EQ(r.view.lastScrub, 28);
    r.ctl.onInput(ev(InputType::TouchHoldRelease, HitZone::Disc), r.now);  // back discards
    CHECK(r.ctl.hasNavigationRequest());
    CHECK(r.ctl.takeNavigationRequest().screen == AppScreen::Info);
    CHECK_EQ(r.pc.seeks, 1);
    r.frame();
    CHECK(r.app.media().state().knobBinding == KnobBinding::Volume);
    CHECK_EQ(r.app.media().state().pendingSeekPercent, -1);
}

TEST_CASE(swipes_skip_tracks_and_leaving_destroys_the_view) {
    Rig r;
    r.ctl.onEnter(r.now);
    CHECK(r.ctl.onInput(gesture(Gesture::SwipeLeft), r.now));
    CHECK_EQ(r.pc.nexts, 1);
    CHECK(r.ctl.onInput(gesture(Gesture::SwipeRight), r.now));
    CHECK_EQ(r.pc.prevs, 1);
    CHECK(r.ctl.onInput(gesture(Gesture::SwipeUp), r.now));  // consumed, nothing happens
    r.ctl.onExit(r.now);
    CHECK(!r.view.built());
}

TEST_CASE(the_media_screen_allocates_nothing) {
    Rig r;
    r.ctl.onEnter(r.now);
    r.makeSeekable();
    NO_HEAP_SCOPE();
    for (int i = 0; i < 300; ++i) {
        r.ctl.onInput(knob((i % 5) - 2), r.now);
        if (i % 40 == 0) r.ctl.onInput(gesture(Gesture::DoubleTap), r.now);
        if (i % 17 == 0) r.tap();
        r.frame();
    }
    CHECK(r.view.renders >= 300);
}
