// INPT-07/08/11 + UIX-06: the pure input core on the host. Ring and overflow, the seven touch
// flags, TOUCH_MOVE every poll, the D13 back gate, the double-tap counter, the stale window, the
// wake consumption, the knob tokens and the hit zones.
#include <cstring>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "input/HitZones.h"
#include "input/InputSfxListener.h"
#include "input/InputTranslator.h"

using namespace input;

namespace {

TouchSample pressAt(int16_t x, int16_t y) {
    TouchSample s;
    s.pressed = true;
    s.newPress = true;
    s.x = x;
    s.y = y;
    return s;
}

TouchSample holdAt(int16_t x, int16_t y) {
    TouchSample s;
    s.pressed = true;
    s.x = x;
    s.y = y;
    return s;
}

TouchSample tapRelease(int16_t x, int16_t y, uint32_t durationMs) {
    TouchSample s;
    s.newRelease = true;
    s.newGesture = true;
    s.gestureCode = kGestureTapRelease;
    s.x = x;
    s.y = y;
    s.pressDurationMs = durationMs;
    return s;
}

TouchSample holdRelease(int16_t x, int16_t y, uint32_t durationMs) {
    TouchSample s;
    s.newHoldRelease = true;
    s.newGesture = true;
    s.gestureCode = kGestureHoldRelease;
    s.x = x;
    s.y = y;
    s.pressDurationMs = durationMs;
    return s;
}

TouchSample swipe(uint8_t code, int16_t x, int16_t y) {
    TouchSample s;
    s.newGesture = true;
    s.gestureCode = code;
    s.x = x;
    s.y = y;
    return s;
}

size_t drain(InputTranslator& t, InputEvent* out, size_t cap) {
    size_t n = 0;
    InputEvent e;
    while (t.next(e)) {
        if (n < cap) out[n] = e;
        ++n;
    }
    return n;
}

struct CountingFeedback : ITouchFeedback {
    int presses = 0, releases = 0, swipes = 0;
    Gesture lastSwipe = Gesture::None;
    void onTouchPress() override { ++presses; }
    void onTouchRelease() override { ++releases; }
    void onSwipe(Gesture g) override { ++swipes; lastSwipe = g; }
};

}  // namespace

TEST_CASE(tap_produces_press_moves_and_release_with_duration) {
    InputTranslator t;
    t.configure({466, 466, false});
    NO_HEAP_SCOPE();
    t.onTouch(pressAt(233, 233), 1000);
    t.onTouch(holdAt(234, 233), 1016);
    t.onTouch(holdAt(235, 233), 1032);
    t.onTouch(tapRelease(235, 233, 80), 1080);
    InputEvent ev[8];
    const size_t n = drain(t, ev, 8);
    CHECK_EQ(n, static_cast<size_t>(4));
    CHECK(ev[0].type == InputType::TouchPress && ev[0].zone == HitZone::Disc);
    CHECK(ev[1].type == InputType::TouchMove && ev[2].type == InputType::TouchMove);
    CHECK(ev[3].type == InputType::TouchRelease && ev[3].durationMs == 80u);
    CHECK_EQ(t.lastActivityMs(), 1080u);
    CHECK_EQ(t.overflow(), 0u);
}

TEST_CASE(back_gate_consumes_short_hold_release_and_passes_long) {
    InputTranslator t;
    t.onTouch(pressAt(233, 233), 0);
    t.onTouch(holdRelease(233, 233, 1000), 1000);
    InputEvent ev[8];
    size_t n = drain(t, ev, 8);
    CHECK_EQ(n, static_cast<size_t>(1));  // only the press; the 1 s hold-release is discarded
    CHECK(ev[0].type == InputType::TouchPress);

    t.onTouch(pressAt(233, 233), 2000);
    t.onTouch(holdRelease(233, 233, 3500), 5500);
    n = drain(t, ev, 8);
    CHECK_EQ(n, static_cast<size_t>(2));
    CHECK(ev[1].type == InputType::TouchHoldRelease && ev[1].durationMs == 3500u);
}

TEST_CASE(double_tap_within_a_second_adds_one_gesture) {
    InputTranslator t;
    InputEvent ev[8];
    t.onTouch(pressAt(233, 233), 0);
    t.onTouch(tapRelease(233, 233, 60), 60);
    t.onTouch(pressAt(233, 233), 400);
    t.onTouch(tapRelease(233, 233, 60), 460);
    size_t n = drain(t, ev, 8);
    CHECK_EQ(n, static_cast<size_t>(5));
    CHECK(ev[4].type == InputType::Gesture && ev[4].gesture == Gesture::DoubleTap);
    // A third tap 2 s later starts over: no gesture.
    t.onTouch(pressAt(233, 233), 3000);
    t.onTouch(tapRelease(233, 233, 60), 3060);
    n = drain(t, ev, 8);
    CHECK_EQ(n, static_cast<size_t>(2));
}

TEST_CASE(stale_window_drops_the_entering_release_and_gesture) {
    InputTranslator t;
    InputEvent ev[8];
    t.onTouch(pressAt(233, 233), 0);
    t.beginStaleWindow(100);  // the screen changed while the finger was still down
    t.onTouch(tapRelease(233, 233, 150), 150);
    t.onTouch(swipe(kGestureSwipeLeft, 233, 233), 200);
    size_t n = drain(t, ev, 8);
    CHECK_EQ(n, static_cast<size_t>(1));  // the press only
    // After the window a new tap is delivered in full.
    t.onTouch(pressAt(233, 233), 600);
    t.onTouch(tapRelease(233, 233, 50), 650);
    n = drain(t, ev, 8);
    CHECK_EQ(n, static_cast<size_t>(2));
}

TEST_CASE(wake_press_and_its_release_are_consumed) {
    InputTranslator t;
    InputEvent ev[8];
    t.armWakeConsumption();
    t.onTouch(pressAt(233, 233), 0);
    t.onTouch(holdAt(233, 233), 16);
    t.onTouch(tapRelease(233, 233, 40), 40);
    CHECK_EQ(drain(t, ev, 8), static_cast<size_t>(0));
    CHECK_EQ(t.lastActivityMs(), 40u);  // it still counts as activity
    t.onTouch(pressAt(233, 233), 500);
    t.onTouch(tapRelease(233, 233, 40), 540);
    CHECK_EQ(drain(t, ev, 8), static_cast<size_t>(2));
}

TEST_CASE(ring_overflow_counts_and_keeps_order) {
    InputTranslator t;
    for (int i = 0; i < 12; ++i) {
        t.onKnobToken("+", static_cast<uint32_t>(i));
    }
    CHECK_EQ(t.pending(), InputTranslator::kRingCapacity);
    CHECK_EQ(t.overflow(), 4u);
    CHECK_EQ(t.highWater(), InputTranslator::kRingCapacity);
    InputEvent ev[8];
    CHECK_EQ(drain(t, ev, 8), InputTranslator::kRingCapacity);
    CHECK_EQ(t.pending(), static_cast<size_t>(0));
}

TEST_CASE(knob_tokens_map_to_deltas_bursts_and_link) {
    InputTranslator t;
    InputEvent ev[8];
    t.onKnobToken("+", 0);
    t.onKnobToken("-", 1);
    t.onKnobToken("+3", 2);
    t.onKnobToken("-12", 3);
    t.onKnobToken("setpoint=42", 4);
    t.onKnobToken("+abc", 5);
    t.onKnobToken("hello", 6);
    const size_t n = drain(t, ev, 8);
    CHECK_EQ(n, static_cast<size_t>(7));
    CHECK(ev[0].type == InputType::KnobDelta && ev[0].knobDelta == 1);
    CHECK(ev[1].type == InputType::KnobDelta && ev[1].knobDelta == -1);
    CHECK(ev[2].type == InputType::KnobDelta && ev[2].knobDelta == 3);
    CHECK(ev[3].type == InputType::KnobDelta && ev[3].knobDelta == -10);  // D5 clamp
    CHECK(ev[4].type == InputType::KnobBurst && ev[4].knobDelta == 42);
    CHECK(ev[5].type == InputType::KnobLink);
    CHECK(ev[6].type == InputType::KnobLink);
}

TEST_CASE(hit_zones_follow_d12) {
    HitGeometry g{466, 466, false};
    CHECK(classifyHit(233, 233, g) == HitZone::Disc);
    CHECK(classifyHit(233 + 131, 233, g) == HitZone::Disc);
    CHECK(classifyHit(233 + 136, 233, g) == HitZone::Dead);
    CHECK(classifyHit(233 + 165, 233, g) == HitZone::Rim);
    CHECK(classifyHit(233 + 190, 233, g) == HitZone::Rim);
    CHECK(classifyHit(233 + 200, 233, g) == HitZone::Dead);
    HitGeometry climate{466, 466, true};
    CHECK(classifyHit(233 + 140, 233, climate) == HitZone::Disc);
    // A 240 px panel scales the same numbers.
    HitGeometry small{240, 240, false};
    CHECK(classifyHit(120, 120, small) == HitZone::Disc);
    CHECK(classifyHit(120 + 85, 120, small) == HitZone::Rim);  // 85 * 466 / 240 = 165
}

TEST_CASE(sfx_listener_clicks_on_press_and_releases_on_release) {
    CountingFeedback fb;
    InputSfxListener sfx(&fb);
    InputEvent e;
    e.type = InputType::TouchPress;
    CHECK(sfx.onEvent(e));
    e.type = InputType::TouchMove;
    CHECK(!sfx.onEvent(e));
    e.type = InputType::TouchRelease;
    CHECK(sfx.onEvent(e));
    e.type = InputType::Gesture;
    e.gesture = Gesture::SwipeLeft;
    CHECK(sfx.onEvent(e));
    e.gesture = Gesture::DoubleTap;
    CHECK(!sfx.onEvent(e));
    CHECK_EQ(fb.presses, 1);
    CHECK_EQ(fb.releases, 1);
    CHECK_EQ(fb.swipes, 1);
    CHECK(fb.lastSwipe == Gesture::SwipeLeft);
}

TEST_CASE(a_lift_the_driver_did_not_classify_is_still_a_release) {
    InputTranslator t;
    InputEvent ev[8];
    t.onTouch(pressAt(233, 233), 0);
    t.onTouch(holdAt(250, 240), 16);   // drifted past the tap radius, short of a swipe
    TouchSample lift;                  // pressed false, no flags: the driver gave up on it
    lift.x = 250;
    lift.y = 240;
    lift.pressDurationMs = 120;
    t.onTouch(lift, 140);
    const size_t n = drain(t, ev, 8);
    CHECK_EQ(n, static_cast<size_t>(3));
    CHECK(ev[2].type == InputType::TouchRelease && ev[2].durationMs == 120u && ev[2].x == 250);
    // Nothing further while the finger stays up.
    t.onTouch(lift, 200);
    CHECK_EQ(drain(t, ev, 8), static_cast<size_t>(0));
}
