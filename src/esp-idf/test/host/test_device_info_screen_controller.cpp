// test_device_info_screen_controller (SCRN-10 / SCRN-11): the shared geometry places the two painted
// bars inside the panel without overlap and keeps the widened hit rects overlapping exactly as the
// old page had them, resolved by test order; the signal glyph reaches all five
// levels the model can report (the old page could not reach the top bar); every battery
// classification is reachable; a bar drag moves the display brightness through the injected
// accessor and the media volume through a record plus the target; the bottom band asks for INFO
// and the edges ask for one matrix step; a swipe is consumed and a knob detent is not; a model
// change reaches the view with no touch at all; and none of it allocates.
#include <cstring>

#include "DeviceInfoScreenController.h"
#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/AppModel.h"

using namespace model;
using input::Gesture;
using input::HitZone;
using input::InputEvent;
using input::InputType;
using ui::deviceinfo::BatteryVisual;

namespace {

class RecordingView final : public ui::ScreenView<ui::DeviceInfoScreenState> {
public:
    int renders = 0;
    ui::DeviceInfoScreenState last;
    lv_obj_t* root() const override { return root_; }

protected:
    void doBuild(lv_obj_t* /*parent*/) override { root_ = reinterpret_cast<lv_obj_t*>(&marker_); }
    void doDestroy() override { root_ = nullptr; }
    void doSetVisible(bool /*visible*/) override {}
    void doRender(const ui::DeviceInfoScreenState& s) override {
        ++renders;
        last = s;
    }

private:
    int marker_ = 0;
    lv_obj_t* root_ = nullptr;
};

class FakeTarget final : public media::IMediaTarget {
public:
    int setVolumeCalls = 0;
    int lastVolume = -1;
    MediaTargetId id() const override { return MediaTargetId::Pc; }
    const char* label() const override { return "fake"; }
    media::MediaCaps caps() const override { return media::MediaCaps{}; }
    bool available() const override { return true; }
    media::MediaError setVolume(int percent, Millis) override {
        ++setVolumeCalls;
        lastVolume = percent;
        return media::MediaError::Ok;
    }
    media::MediaError play(Millis) override { return media::MediaError::Ok; }
    media::MediaError pause(Millis) override { return media::MediaError::Ok; }
    media::MediaError toggle(Millis) override { return media::MediaError::Ok; }
    media::MediaError next(Millis) override { return media::MediaError::Ok; }
    media::MediaError previous(Millis) override { return media::MediaError::Ok; }
    media::MediaError seekAbsolute(int, Millis) override { return media::MediaError::Ok; }
    media::MediaError seekRelative(int32_t, Millis) override { return media::MediaError::Ok; }
    media::MediaError requestState(Millis) override { return media::MediaError::Ok; }
    void tick(Millis) override {}
};

int g_brightness = 100;
int brightnessGet(void*) { return g_brightness; }
void brightnessSet(int percent, void*) { g_brightness = percent; }

InputEvent at(InputType type, int x, int y) {
    InputEvent e;
    e.type = type;
    e.x = static_cast<int16_t>(x);
    e.y = static_cast<int16_t>(y);
    e.zone = HitZone::Disc;
    return e;
}

struct Rig {
    AppModel app;
    FakeTarget pc;
    media::MediaTargetRegistry reg;
    RecordingView view;
    controllers::DeviceInfoScreenController ctl{app, reg, &view, 466, 466};
    ui::deviceinfo::Layout layout = ui::deviceinfo::layoutFor(466, 466);
    uint32_t now = 1000;
    Rig() {
        reg.registerTarget(&pc);
        g_brightness = 100;
        ctl.setBrightnessAccess(&brightnessGet, &brightnessSet, nullptr);
    }
    void frame() {
        now += 16;
        app.drainPendingMutations(now);
        ctl.tick(now);
    }
};

}  // namespace

TEST_CASE(the_shared_geometry_places_the_bars_and_the_regions_inside_the_panel) {
    NO_HEAP_SCOPE();
    const ui::deviceinfo::Layout l = ui::deviceinfo::layoutFor(466, 466);
    CHECK(l.brightnessBar.x > l.leftEdge.w);                       // the left edge does not cover a bar
    CHECK(l.brightnessBar.x + l.brightnessBar.w < l.soundBar.x);   // the bars do not overlap
    CHECK(l.soundBar.x + l.soundBar.w < l.rightEdge.x);
    CHECK(l.brightnessBar.y > 0 && l.brightnessBar.y + l.brightnessBar.h < l.panelH);
    CHECK(l.brightnessBar.h == l.soundBar.h && l.brightnessBar.y == l.soundBar.y);
    CHECK(l.soundBar.y + l.soundBar.h < l.bottomBand.y);           // the band is below both bars
    CHECK(l.iconX + l.iconW <= 466 - l.edgeW);
    CHECK(l.bluetoothY < l.wifiY && l.wifiY < l.batteryY);
    CHECK(ui::deviceinfo::contains(l.brightnessTouch, l.brightnessBar.x, l.brightnessBar.y));
    CHECK(!ui::deviceinfo::contains(l.soundTouch, l.brightnessBar.x, l.brightnessBar.y));
    // The bar reads 100 % at its top edge and 0 % at its bottom, clamped outside.
    CHECK_EQ(ui::deviceinfo::percentFromBarY(l.soundBar.y, l.soundBar.y, l.soundBar.h), 100);
    CHECK_EQ(ui::deviceinfo::percentFromBarY(l.soundBar.y + l.soundBar.h, l.soundBar.y, l.soundBar.h), 0);
    CHECK_EQ(ui::deviceinfo::percentFromBarY(-50, l.soundBar.y, l.soundBar.h), 100);
    CHECK_EQ(ui::deviceinfo::percentFromBarY(9999, l.soundBar.y, l.soundBar.h), 0);
    // A 240 px panel still yields drawable geometry (the reference space itself).
    const ui::deviceinfo::Layout small = ui::deviceinfo::layoutFor(240, 240);
    CHECK(small.brightnessBar.w > 0 && small.brightnessBar.h > 0);
    CHECK(small.soundBar.x + small.soundBar.w <= 240);
}

TEST_CASE(the_signal_glyph_reaches_every_level_the_model_can_report) {
    NO_HEAP_SCOPE();
    ConnectivityState c;
    c.wifiConnected = false;
    CHECK_EQ(ui::deviceinfo::signalLevels(c), 4);  // a lost link lights every level (painted red)
    c.wifiConnected = true;
    for (int bars = 0; bars <= 4; ++bars) {
        c.bars = static_cast<SignalBars>(bars);
        CHECK_EQ(ui::deviceinfo::signalLevels(c), bars);  // 4 was unreachable on the old page
    }
    c.bars = 9;
    CHECK_EQ(ui::deviceinfo::signalLevels(c), 4);
}

TEST_CASE(every_battery_classification_is_reachable) {
    NO_HEAP_SCOPE();
    PowerState p;
    CHECK(ui::deviceinfo::batteryVisualFor(p) == BatteryVisual::Unknown);
    p.presenceKnown = true;
    CHECK(ui::deviceinfo::batteryVisualFor(p) == BatteryVisual::Absent);
    p.connected = true;
    CHECK(ui::deviceinfo::batteryVisualFor(p) == BatteryVisual::NoReading);
    p.percentValid = true;
    p.percent = 15.0f;
    p.band = BatteryBand::Critical;
    CHECK(ui::deviceinfo::batteryVisualFor(p) == BatteryVisual::Critical);
    p.percent = 80.0f;
    p.band = BatteryBand::Good;
    CHECK(ui::deviceinfo::batteryVisualFor(p) == BatteryVisual::Ok);
    p.charging = true;
    CHECK(ui::deviceinfo::batteryVisualFor(p) == BatteryVisual::Charging);
}

TEST_CASE(a_drag_on_the_left_bar_moves_the_display_brightness) {
    Rig r;
    r.ctl.onEnter(r.now);
    CHECK(r.view.built());
    const ui::deviceinfo::Rect& bar = r.layout.brightnessBar;
    const int midX = bar.x + bar.w / 2;
    CHECK(r.ctl.onInput(at(InputType::TouchPress, midX, bar.y + bar.h / 4), r.now));
    CHECK_EQ(g_brightness, 75);
    CHECK(r.ctl.onInput(at(InputType::TouchMove, midX, bar.y + bar.h - 1), r.now));  // the hit rect is half-open, as the page had it
    CHECK_EQ(g_brightness, 0);
    CHECK(r.ctl.onInput(at(InputType::TouchMove, midX, bar.y), r.now));
    CHECK_EQ(g_brightness, 100);
    r.frame();
    CHECK_EQ(r.view.last.brightnessPercent, 100);
    CHECK_EQ(r.pc.setVolumeCalls, 0);  // the left bar never touches the volume
    // A press outside both hit rects changes nothing. 233 is the panel centre and therefore the
    // middle of the sound bar, so the dead point is to the right of it, short of the edge column.
    CHECK(r.ctl.onInput(at(InputType::TouchPress, 350, 233), r.now));
    CHECK_EQ(g_brightness, 100);
    CHECK_EQ(r.pc.setVolumeCalls, 0);
    r.frame();
    CHECK_EQ(r.app.media().state().volume, 50);
}

TEST_CASE(a_drag_on_the_right_bar_moves_the_media_volume_through_a_record_and_the_target) {
    Rig r;
    r.ctl.onEnter(r.now);
    const ui::deviceinfo::Rect& bar = r.layout.soundBar;
    const int midX = bar.x + bar.w / 2;
    // The bar starts at the model's 50 %, so a drag to 50 % is not a change and sends nothing.
    CHECK(r.ctl.onInput(at(InputType::TouchPress, midX, bar.y + bar.h / 2), r.now));
    r.frame();
    CHECK_EQ(r.app.media().state().volume, 50);
    CHECK_EQ(r.pc.setVolumeCalls, 0);
    CHECK(r.ctl.onInput(at(InputType::TouchMove, midX, bar.y + bar.h / 4), r.now));
    r.frame();
    CHECK_EQ(r.app.media().state().volume, 75);
    CHECK_EQ(r.pc.lastVolume, 75);
    CHECK(r.ctl.onInput(at(InputType::TouchMove, midX, bar.y + bar.h / 10), r.now));
    r.frame();
    CHECK_EQ(r.app.media().state().volume, 90);
    CHECK_EQ(r.pc.lastVolume, 90);
    CHECK_EQ(r.view.last.soundPercent, 90);
    CHECK_EQ(g_brightness, 100);  // the right bar never touches the brightness
}

TEST_CASE(the_bottom_band_asks_for_info_and_the_edges_ask_for_one_matrix_step) {
    Rig r;
    r.ctl.onEnter(r.now);
    CHECK(!r.ctl.hasNavigationRequest());
    CHECK(!r.ctl.hasPageStep());
    CHECK(r.ctl.onInput(at(InputType::TouchRelease, 233, r.layout.bottomBand.y + 5), r.now));
    CHECK(r.ctl.hasNavigationRequest());
    const controllers::NavigationRequest request = r.ctl.takeNavigationRequest();
    CHECK(request.valid() && request.screen == AppScreen::Info);
    CHECK(!r.ctl.hasNavigationRequest());  // destructive, once
    CHECK(r.ctl.onInput(at(InputType::TouchRelease, 5, 233), r.now));
    CHECK_EQ(r.ctl.takePageStep(), -1);
    CHECK_EQ(r.ctl.takePageStep(), 0);
    CHECK(r.ctl.onInput(at(InputType::TouchRelease, 461, 233), r.now));
    CHECK_EQ(r.ctl.takePageStep(), 1);
    // A hold-release does the same as a tap on the band, as the page has always done.
    CHECK(r.ctl.onInput(at(InputType::TouchHoldRelease, 233, r.layout.bottomBand.y + 5), r.now));
    CHECK(r.ctl.takeNavigationRequest().screen == AppScreen::Info);
    // A release in the middle does nothing at all.
    CHECK(r.ctl.onInput(at(InputType::TouchRelease, 233, 233), r.now));
    CHECK(!r.ctl.hasNavigationRequest());
    CHECK(!r.ctl.hasPageStep());
}

TEST_CASE(a_swipe_is_consumed_and_a_knob_detent_falls_through) {
    Rig r;
    r.ctl.onEnter(r.now);
    InputEvent swipe;
    swipe.type = InputType::Gesture;
    swipe.gesture = Gesture::SwipeUp;
    CHECK(r.ctl.onInput(swipe, r.now));  // consumed: no matrix swipe on this page
    CHECK(!r.ctl.hasNavigationRequest());
    CHECK(!r.ctl.hasPageStep());
    InputEvent knob;
    knob.type = InputType::KnobDelta;
    knob.knobDelta = 3;
    CHECK(!r.ctl.onInput(knob, r.now));  // the router's default handling still applies
    r.ctl.onExit(r.now);
    CHECK(!r.view.built());
}

TEST_CASE(a_model_change_reaches_the_view_with_no_touch) {
    Rig r;
    r.ctl.onEnter(r.now);
    r.frame();
    CHECK(r.view.last.connectivity != nullptr && !r.view.last.connectivity->wifiConnected);
    CHECK_EQ(static_cast<int>(r.view.last.connectivity->bars), 0);
    r.app.mutations().post(ConnectivityModel::make(ConnectivityModel::Field::Wifi, 1, r.now));
    r.app.mutations().post(ConnectivityModel::make(ConnectivityModel::Field::Strength, 4, r.now));
    r.app.mutations().post(ConnectivityModel::make(ConnectivityModel::Field::Internet, 1, r.now));
    r.frame();
    CHECK(r.view.last.connectivity->wifiConnected);
    CHECK(r.view.last.connectivity->internetAvailable);
    CHECK_EQ(ui::deviceinfo::signalLevels(*r.view.last.connectivity), 4);
    r.app.mutations().post(PowerModel::make(PowerModel::Field::PresenceKnown, 1, r.now));
    r.app.mutations().post(PowerModel::make(PowerModel::Field::Connected, 1, r.now));
    r.app.mutations().post(PowerModel::makeFloat(PowerModel::Field::Percent, 12.0f, r.now));
    r.frame();
    CHECK(ui::deviceinfo::batteryVisualFor(*r.view.last.power) == BatteryVisual::Critical);
    r.app.mutations().post(ConnectivityModel::make(ConnectivityModel::Field::Wifi, 0, r.now));
    r.frame();
    CHECK(!r.view.last.connectivity->wifiConnected);  // pulling the AP flips the glyph, no touch
    CHECK(r.view.renders >= 4);
}

TEST_CASE(the_device_info_page_allocates_nothing) {
    Rig r;
    r.ctl.onEnter(r.now);
    const ui::deviceinfo::Rect& bar = r.layout.soundBar;
    NO_HEAP_SCOPE();
    for (int i = 0; i < 400; ++i) {
        r.ctl.onInput(at(InputType::TouchMove, bar.x + 2, bar.y + (i % bar.h)), r.now);
        if (i % 37 == 0) {
            r.app.mutations().post(ConnectivityModel::make(ConnectivityModel::Field::Strength, i % 5, r.now));
        }
        r.frame();
    }
    CHECK(r.view.renders >= 400);
}

TEST_CASE(a_drag_that_leaves_the_bar_freezes_the_value_and_a_null_accessor_is_harmless) {
    Rig r;
    r.ctl.onEnter(r.now);
    const ui::deviceinfo::Rect& bar = r.layout.soundBar;
    const int midX = bar.x + bar.w / 2;
    CHECK(r.ctl.onInput(at(InputType::TouchPress, midX, bar.y + bar.h / 4), r.now));
    r.frame();
    CHECK_EQ(r.app.media().state().volume, 75);
    // Off the bar: the value freezes, exactly as the page behaved (no clamp to an edge).
    CHECK(r.ctl.onInput(at(InputType::TouchMove, midX, bar.y - 40), r.now));
    CHECK(r.ctl.onInput(at(InputType::TouchMove, 5, 5), r.now));
    r.frame();
    CHECK_EQ(r.app.media().state().volume, 75);
    // A hold-release on an edge steps the page like a plain release.
    CHECK(r.ctl.onInput(at(InputType::TouchHoldRelease, 461, 233), r.now));
    CHECK_EQ(r.ctl.takePageStep(), 1);
    // With no brightness accessor the page still renders and still reports a percent.
    controllers::DeviceInfoScreenController bare{r.app, r.reg, nullptr, 466, 466};
    bare.onEnter(r.now);
    CHECK(bare.onInput(at(InputType::TouchPress, r.layout.brightnessBar.x + 2, r.layout.brightnessBar.y), r.now));
    bare.tick(r.now);  // a null view is not a crash
    bare.onExit(r.now);
    CHECK(!bare.hasNavigationRequest());
}

TEST_CASE(an_adc_estimate_with_no_presence_still_shows_a_reading) {
    NO_HEAP_SCOPE();
    // The ADC boards estimate the percent from the voltage and never learn presence (and PERI-05
    // left presenceKnown false on every board): the reading must still colour the icon, which is
    // what the old page did with its connected-and-valid gate.
    PowerState p;
    p.presenceKnown = false;
    p.connected = true;
    p.percentValid = true;
    p.percent = 64.0f;
    p.band = BatteryBand::Good;
    CHECK(ui::deviceinfo::batteryVisualFor(p) == BatteryVisual::Ok);
    p.percent = 8.0f;
    p.band = BatteryBand::Critical;
    CHECK(ui::deviceinfo::batteryVisualFor(p) == BatteryVisual::Critical);
    p.charging = true;
    CHECK(ui::deviceinfo::batteryVisualFor(p) == BatteryVisual::Charging);
    // A percent with no connection is still no reading.
    p.charging = false;
    p.connected = false;
    CHECK(ui::deviceinfo::batteryVisualFor(p) == BatteryVisual::Unknown);
    p.presenceKnown = true;
    CHECK(ui::deviceinfo::batteryVisualFor(p) == BatteryVisual::Absent);
}

TEST_CASE(a_held_finger_does_no_work_after_the_first_change) {
    Rig r;
    r.ctl.onEnter(r.now);
    const ui::deviceinfo::Rect& sound = r.layout.soundBar;
    const ui::deviceinfo::Rect& bright = r.layout.brightnessBar;
    const int y = sound.y + sound.h / 4;
    CHECK(r.ctl.onInput(at(InputType::TouchPress, sound.x + 2, y), r.now));
    r.frame();
    CHECK_EQ(r.pc.setVolumeCalls, 1);
    for (int i = 0; i < 20; ++i) {  // the same y, once per frame, as a resting finger produces
        CHECK(r.ctl.onInput(at(InputType::TouchMove, sound.x + 2, y), r.now));
        r.frame();
    }
    CHECK_EQ(r.pc.setVolumeCalls, 1);  // one call, not twenty-one
    CHECK_EQ(r.app.media().state().volume, 75);
    g_brightness = 100;
    CHECK(r.ctl.onInput(at(InputType::TouchPress, bright.x + 2, bright.y), r.now));
    CHECK_EQ(g_brightness, 100);  // already there: no setter call, so no panel write and no log
    CHECK(r.ctl.onInput(at(InputType::TouchPress, bright.x + 2, bright.y + bright.h / 2), r.now));
    CHECK_EQ(g_brightness, 50);
}

TEST_CASE(the_widened_hit_rects_overlap_the_edge_and_each_other_in_the_ported_order) {
    Rig r;
    r.ctl.onEnter(r.now);
    const ui::deviceinfo::Layout& l = r.layout;
    // The two hit rects are the painted bars widened by 8 reference px on each side, so they reach
    // into the left edge column and into each other, exactly as the old page's did. The order of
    // the tests decides who wins, and that order is what these two presses pin.
    CHECK(l.brightnessTouch.x < l.leftEdge.w);
    CHECK(l.soundTouch.x < l.brightnessTouch.x + l.brightnessTouch.w);
    const int inEdge = l.leftEdge.w - 1;
    CHECK(ui::deviceinfo::contains(l.brightnessTouch, inEdge, 200));
    CHECK(r.ctl.onInput(at(InputType::TouchPress, inEdge, l.brightnessBar.y), r.now));
    CHECK_EQ(g_brightness, 100);  // the top of the bar: already there
    CHECK(r.ctl.onInput(at(InputType::TouchPress, inEdge, l.brightnessBar.y + l.brightnessBar.h / 2), r.now));
    CHECK_EQ(g_brightness, 50);   // a press in the overlap drives the bar, not the page
    CHECK(!r.ctl.hasPageStep());
    // The release at the same point is the edge, though: the page steps while the bar moved.
    CHECK(r.ctl.onInput(at(InputType::TouchRelease, inEdge, 200), r.now));
    CHECK_EQ(r.ctl.takePageStep(), -1);
    // In the 12 px seam between the two hit rects the brightness wins, as the first test.
    const int seam = l.brightnessTouch.x + l.brightnessTouch.w - 1;
    CHECK(ui::deviceinfo::contains(l.soundTouch, seam, 200));
    const int volumeCalls = r.pc.setVolumeCalls;
    CHECK(r.ctl.onInput(at(InputType::TouchPress, seam, l.brightnessBar.y + l.brightnessBar.h / 4), r.now));
    CHECK_EQ(g_brightness, 75);
    CHECK_EQ(r.pc.setVolumeCalls, volumeCalls);
    // A bottom corner is the band, not the edge, because the band is tested first.
    CHECK(r.ctl.onInput(at(InputType::TouchRelease, 2, l.bottomBand.y + 2), r.now));
    CHECK(r.ctl.takeNavigationRequest().screen == AppScreen::Info);
    CHECK(!r.ctl.hasPageStep());
}

TEST_CASE(an_unpolled_back_request_does_not_outlive_the_screen) {
    Rig r;
    r.ctl.onEnter(r.now);
    // The router consumes a pending mode change before it reaches this screen's case, so a tap on
    // the way out can leave an intent nobody read. It must not fire on the next entry.
    CHECK(r.ctl.onInput(at(InputType::TouchRelease, 233, r.layout.bottomBand.y + 5), r.now));
    CHECK(r.ctl.hasNavigationRequest());
    r.ctl.onExit(r.now);
    CHECK(!r.ctl.hasNavigationRequest());
    CHECK(r.ctl.onInput(at(InputType::TouchRelease, 233, r.layout.bottomBand.y + 5), r.now));
    r.ctl.onEnter(r.now);
    CHECK(!r.ctl.hasNavigationRequest());
    CHECK(!r.ctl.hasPageStep());
}
