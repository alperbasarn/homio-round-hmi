// test_light_model (MDL-17): 0..100 only, the single wire-scale rule, hue at the wrap boundary,
// the first-ever execution of the LedMode cycle, one publish per window, zero allocations.
#include <cmath>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/ColorMath.h"
#include "model/LightModel.h"

using namespace model;

namespace {

void countNotify(const LightState&, void* context) { ++*static_cast<int*>(context); }

}  // namespace

TEST_CASE(brightness_is_0_to_100_only) {
    LightModel l;
    CHECK_EQ(l.state().brightness, 50);
    CHECK(!l.setBrightness(101));
    CHECK(!l.setBrightness(-1));
    CHECK_EQ(l.state().brightness, 50);
    CHECK(l.setBrightness(100));
    CHECK(l.setBrightness(0));
    CHECK(l.adjustBrightness(-1) == false);      // already at the floor
    CHECK(l.adjustBrightness(150));
    CHECK_EQ(l.state().brightness, 100);
}

TEST_CASE(inbound_0_to_255_goes_through_the_single_wire_scale_rule) {
    LightModel l;
    CHECK(!l.setBrightnessFromWire(256));
    CHECK(!l.setBrightnessFromWire(-1));
    CHECK(l.setBrightnessFromWire(255));
    CHECK_EQ(l.state().brightness, 100);
    CHECK(l.setBrightnessFromWire(127));
    CHECK_EQ(l.state().brightness, 50);
    CHECK(l.setBrightnessFromWire(0));
    CHECK_EQ(l.state().brightness, 0);
    CHECK_EQ(LightModel::wireFromPercent(0), 0);
    CHECK_EQ(LightModel::wireFromPercent(50), 127);   // what LightController always published
    CHECK_EQ(LightModel::wireFromPercent(100), 255);
    int asymmetric = 0;
    for (int p = 0; p <= 100; ++p) {
        if (LightModel::percentFromWire(LightModel::wireFromPercent(p)) != p) ++asymmetric;
    }
    CHECK_EQ(asymmetric, 0);
}

TEST_CASE(hue_clamps_at_the_wrap_boundary) {
    LightModel l;
    CHECK(l.setHue(1.5f));
    CHECK_EQ(l.state().hue, 1.0f);
    CHECK_EQ(l.swatchRgb(), hsvToRgb(0.0f, 1.0f, 1.0f));  // i % 6 wraps 1.0 onto 0.0: red
    CHECK(l.setHue(-1.0f));
    CHECK_EQ(l.state().hue, 0.0f);
    CHECK(!l.setHue(std::nanf("")));
    CHECK(l.setSaturation(2.0f));
    CHECK_EQ(l.state().saturation, 1.0f);
    CHECK(l.setBrightness(50));
    CHECK(l.setHue(0.5f));
    CHECK_EQ(l.publishedRgb(), hsvToRgb(0.5f, 1.0f, 0.5f));
    CHECK_EQ(l.swatchRgb(), hsvToRgb(0.5f, 1.0f, 1.0f));
}

TEST_CASE(led_mode_cycles_static_glow_rainbow) {
    LightModel l;
    CHECK(l.state().mode == LedMode::Static);
    CHECK(l.cycleMode() == LedMode::Glow);
    CHECK(l.cycleMode() == LedMode::Rainbow);
    CHECK(l.cycleMode() == LedMode::Static);
    CHECK(l.setMode(LedMode::Rainbow));
    CHECK(!l.setMode(LedMode::Count));
    CHECK(l.state().mode == LedMode::Rainbow);
}

TEST_CASE(one_publish_per_window) {
    LightModel l;
    l.markChanged();
    CHECK(l.shouldPublish(0));
    int fires = 0;
    for (int t = 1; t < 25; ++t) {
        l.markChanged();
        if (l.shouldPublish(t)) ++fires;
    }
    CHECK_EQ(fires, 0);
    CHECK(l.publishPending());
    CHECK(l.shouldPublish(25));
    CHECK(!l.publishPending());
    l.markChanged();
    CHECK(l.flushPublish());
}

TEST_CASE(light_model_allocates_nothing) {
    NO_HEAP_SCOPE();
    LightModel l;
    int notified = 0;
    CHECK(l.subscribe(&countNotify, &notified));
    for (int i = 0; i < 5000; ++i) {
        l.setBrightness(i % 101);
        l.setHue(static_cast<float>(i % 100) / 100.0f);
        if (i % 7 == 0) l.cycleMode();
        l.commit();
    }
    CHECK(notified > 0);
}
