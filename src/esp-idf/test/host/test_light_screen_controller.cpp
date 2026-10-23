// test_light_screen_controller (CTRL-22 / LED-05 / LED-04 screen half): every control reaches the
// models only. Disc tap toggles power, knob moves brightness, swipes step the effect, a rim drag
// sets the hue and its lift is not a swipe, a rim tap selects the strip under the segment, a hold
// after no drag backs out, a hold on an unpaired segment starts pairing, the knob dials the blink
// count while a pairing is requested and a tap confirms it, and none of it allocates.
#include <cmath>

#include "HeapGuard.h"
#include "LightScreenController.h"
#include "TestRunner.h"
#include "model/AppModel.h"

using namespace model;
using input::Gesture;
using input::HitZone;
using input::InputEvent;
using input::InputType;

namespace {

class RecordingView final : public ui::ScreenView<ui::LightScreenState> {
public:
    int renders = 0;
    int lastDial = -1;
    lv_obj_t* root() const override { return root_; }

protected:
    void doBuild(lv_obj_t* /*parent*/) override { root_ = reinterpret_cast<lv_obj_t*>(&marker_); }
    void doDestroy() override { root_ = nullptr; }
    void doSetVisible(bool /*visible*/) override {}
    void doRender(const ui::LightScreenState& s) override {
        ++renders;
        lastDial = s.dial;
    }

private:
    int marker_ = 0;
    lv_obj_t* root_ = nullptr;
};

InputEvent ev(InputType type, HitZone zone, int x, int y) {
    InputEvent e;
    e.type = type;
    e.zone = zone;
    e.x = static_cast<int16_t>(x);
    e.y = static_cast<int16_t>(y);
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
    RecordingView view;
    controllers::LightScreenController ctl{app, &view, 466, 466};
    uint32_t now = 1000;
    void frame() {
        now += 16;
        app.drainPendingMutations(now);
        ctl.tick(now);
    }
    int addStrip(const char* id, bool paired) {
        const int slot = app.ledRegistry().announce(id, id, now);
        app.ledRegistry().setOnline(slot, true, now);
        app.ledRegistry().setPaired(slot, paired);
        return slot;
    }
};

}  // namespace

TEST_CASE(disc_tap_toggles_power_and_the_knob_moves_brightness) {
    Rig r;
    r.ctl.onEnter(r.now);
    CHECK(r.view.built());
    CHECK(r.ctl.onInput(ev(InputType::TouchPress, HitZone::Disc, 233, 233), r.now));
    CHECK(r.ctl.onInput(ev(InputType::TouchRelease, HitZone::Disc, 233, 233), r.now));
    r.frame();
    CHECK(!r.app.light().state().power);
    CHECK_EQ(r.app.light().pendingFields() & LightModel::PendingPower, static_cast<uint16_t>(LightModel::PendingPower));
    CHECK(r.ctl.onInput(knob(3), r.now));
    CHECK(r.ctl.onInput(knob(-1), r.now));
    r.frame();
    CHECK_EQ(r.app.light().state().brightness, 52);
    CHECK(r.view.renders >= 2);
}

TEST_CASE(swipes_step_the_effect_and_a_rim_drag_sets_the_hue_without_stepping) {
    Rig r;
    r.ctl.onEnter(r.now);
    CHECK(r.ctl.onInput(ev(InputType::TouchPress, HitZone::Disc, 233, 233), r.now));
    CHECK(r.ctl.onInput(gesture(Gesture::SwipeRight), r.now));
    r.frame();
    CHECK(r.app.light().state().effect == LedEffect::Glow);
    CHECK(r.ctl.onInput(ev(InputType::TouchPress, HitZone::Disc, 233, 233), r.now));
    CHECK(r.ctl.onInput(gesture(Gesture::SwipeLeft), r.now));
    r.frame();
    CHECK(r.app.light().state().effect == LedEffect::Solid);
    // A ring drag: press on the rim at 12 o'clock, move to 3 o'clock, lift as a swipe.
    CHECK(r.ctl.onInput(ev(InputType::TouchPress, HitZone::Rim, 233, 68), r.now));
    CHECK(r.ctl.onInput(ev(InputType::TouchMove, HitZone::Rim, 300, 90), r.now));
    CHECK(r.ctl.onInput(ev(InputType::TouchMove, HitZone::Rim, 398, 233), r.now));
    CHECK(r.ctl.dragging());
    CHECK(r.ctl.onInput(gesture(Gesture::SwipeRight), r.now));
    r.frame();
    CHECK(std::fabs(r.app.light().state().hue - 0.25f) < 0.01f);
    CHECK(r.app.light().state().effect == LedEffect::Solid);  // the lift was not a swipe
    CHECK(!r.ctl.dragging());
}

TEST_CASE(a_rim_tap_selects_the_strip_under_the_segment) {
    Rig r;
    const int a = r.addStrip("led-aaaaaa", true);
    const int b = r.addStrip("led-bbbbbb", true);
    r.frame();
    r.ctl.onEnter(r.now);
    CHECK(r.ctl.onInput(ev(InputType::TouchPress, HitZone::Rim, 233, 398), r.now));  // 6 o'clock: second half
    CHECK(r.ctl.onInput(ev(InputType::TouchRelease, HitZone::Rim, 233, 398), r.now));
    r.frame();
    CHECK_EQ(r.app.ledRegistry().selectedIndex(), b);
    CHECK_EQ(r.app.light().state().stripSlot, b);
    CHECK(r.ctl.onInput(ev(InputType::TouchPress, HitZone::Rim, 233, 68), r.now));  // 12 o'clock: first half
    CHECK(r.ctl.onInput(ev(InputType::TouchRelease, HitZone::Rim, 233, 68), r.now));
    r.frame();
    CHECK_EQ(r.app.ledRegistry().selectedIndex(), a);
}

TEST_CASE(a_hold_backs_out_unless_it_follows_a_drag_or_lands_on_an_unpaired_strip) {
    Rig r;
    const int a = r.addStrip("led-aaaaaa", false);  // available, not mine
    r.frame();
    r.ctl.onEnter(r.now);
    CHECK(r.ctl.onInput(ev(InputType::TouchPress, HitZone::Disc, 233, 233), r.now));
    CHECK(r.ctl.onInput(ev(InputType::TouchMove, HitZone::Disc, 260, 233), r.now));
    CHECK(r.ctl.onInput(ev(InputType::TouchHoldRelease, HitZone::Disc, 260, 233), r.now));
    CHECK(!r.ctl.hasNavigationRequest());  // drag-and-hold never backs out
    CHECK(r.ctl.onInput(ev(InputType::TouchPress, HitZone::Rim, 233, 68), r.now));
    CHECK(r.ctl.onInput(ev(InputType::TouchHoldRelease, HitZone::Rim, 233, 68), r.now));
    r.frame();
    CHECK(!r.ctl.hasNavigationRequest());
    CHECK_EQ(r.app.ledRegistry().takePairIntent(), a);  // the pair affordance
    CHECK(r.ctl.onInput(ev(InputType::TouchPress, HitZone::Disc, 233, 233), r.now));
    CHECK(r.ctl.onInput(ev(InputType::TouchHoldRelease, HitZone::Disc, 233, 233), r.now));
    CHECK(r.ctl.hasNavigationRequest());
    CHECK(r.ctl.takeNavigationRequest().screen == AppScreen::Info);
}

TEST_CASE(the_knob_dials_the_blink_count_and_a_tap_confirms_it) {
    Rig r;
    const int a = r.addStrip("led-aaaaaa", false);
    r.app.ledRegistry().setPairing(LedPairing::Requested, a);
    r.frame();
    r.ctl.onEnter(r.now);
    CHECK(r.ctl.onInput(knob(1), r.now));
    CHECK_EQ(r.ctl.dial(), 1);
    CHECK(r.ctl.onInput(knob(2), r.now));
    CHECK_EQ(r.ctl.dial(), 3);
    CHECK(r.ctl.onInput(knob(9), r.now));
    CHECK_EQ(r.ctl.dial(), 6);
    r.frame();
    CHECK_EQ(r.view.lastDial, 6);
    CHECK_EQ(r.app.light().state().brightness, 50);  // the knob did not touch brightness
    CHECK(r.ctl.onInput(ev(InputType::TouchPress, HitZone::Disc, 233, 233), r.now));
    CHECK(r.ctl.onInput(ev(InputType::TouchRelease, HitZone::Disc, 233, 233), r.now));
    r.frame();
    CHECK_EQ(r.app.ledRegistry().takeCodeIntent(), 6);
    CHECK(r.app.light().state().power);  // and the tap was not a power toggle
    r.app.ledRegistry().setPairing(LedPairing::Paired, a);
    r.frame();
    CHECK_EQ(r.ctl.dial(), 0);
}

TEST_CASE(the_input_path_allocates_nothing) {
    Rig r;
    r.addStrip("led-aaaaaa", true);
    r.frame();
    r.ctl.onEnter(r.now);
    NO_HEAP_SCOPE();
    for (int i = 0; i < 300; ++i) {
        r.ctl.onInput(knob(i % 2 ? 1 : -1), r.now);
        r.ctl.onInput(ev(InputType::TouchPress, HitZone::Rim, 233, 68), r.now);
        r.ctl.onInput(ev(InputType::TouchMove, HitZone::Rim, 300 + (i % 50), 100), r.now);
        r.ctl.onInput(gesture(Gesture::SwipeRight), r.now);
        r.frame();
    }
    CHECK(r.view.renders >= 300);
    r.ctl.onExit(r.now);
    CHECK(!r.view.built());
}
