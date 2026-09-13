// test_light_model (MDL-17, reshaped by LED-02): 0..100 only, the single wire-scale rule, hue and
// colour following each other, the eight-effect wrap, pending fields with a fresh seq per take,
// echo suppression on src/seq, the 100 ms window with a trailing flush, zero allocations.
#include <cmath>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/ColorMath.h"
#include "model/LightModel.h"

using namespace model;

namespace {

void countNotify(const LightState&, void* context) { ++*static_cast<int*>(context); }

LedStripState reported(bool power, int brightness, uint32_t rgb, LedEffect effect) {
    LedStripState s;
    s.power = power;
    s.brightness = static_cast<Percent>(brightness);
    s.rgb = rgb;
    s.effect = effect;
    return s;
}

}  // namespace

TEST_CASE(brightness_is_0_to_100_only) {
    LightModel l;
    CHECK_EQ(l.state().brightness, 50);
    CHECK(!l.setBrightness(101));
    CHECK(!l.setBrightness(-1));
    CHECK_EQ(l.state().brightness, 50);
    CHECK(l.setBrightness(100));
    CHECK(l.setBrightness(0));
    CHECK(l.adjustBrightness(-1) == false);  // already at the floor
    CHECK(l.adjustBrightness(150));
    CHECK_EQ(l.state().brightness, 100);
    CHECK(!l.setBrightnessFromWire(256));
    CHECK(l.setBrightnessFromWire(127));
    CHECK_EQ(l.state().brightness, 50);
    CHECK_EQ(LightModel::wireFromPercent(50), 127);
    int asymmetric = 0;
    for (int p = 0; p <= 100; ++p) {
        if (LightModel::percentFromWire(LightModel::wireFromPercent(p)) != p) ++asymmetric;
    }
    CHECK_EQ(asymmetric, 0);
}

TEST_CASE(hue_and_colour_follow_each_other) {
    LightModel l;
    CHECK_EQ(l.state().rgb, 0x00FFFFu);  // the old default swatch
    CHECK(l.setHue(1.5f));
    CHECK_EQ(l.state().hue, 1.0f);
    CHECK_EQ(l.swatchRgb(), hsvToRgb(0.0f, 1.0f, 1.0f));  // 1.0 wraps onto red
    CHECK(!l.setHue(std::nanf("")));
    CHECK(l.setColor(0x00FF00));
    CHECK(std::fabs(l.state().hue - 1.0f / 3.0f) < 0.002f);
    CHECK(l.setColor(0x808080));
    CHECK_EQ(l.state().hue, 0.0f);  // a grey has no hue
    CHECK(l.setBrightness(50));
    CHECK(l.setHue(0.5f));
    CHECK_EQ(l.publishedRgb(), hsvToRgb(0.5f, 1.0f, 0.5f));
    CHECK_EQ(l.swatchRgb(), 0x00FFFFu);
}

TEST_CASE(effects_step_around_the_eight_wire_names) {
    LightModel l;
    CHECK(l.state().effect == LedEffect::Solid);
    CHECK(l.stepEffect(1) == LedEffect::Glow);
    CHECK(l.stepEffect(-2) == LedEffect::Aurora);
    CHECK(l.stepEffect(1) == LedEffect::Solid);
    CHECK(l.setEffect(LedEffect::Fire));
    CHECK(!l.setEffect(LedEffect::Count));
    LedEffect e = LedEffect::Solid;
    CHECK(ledEffectFromName("aurora", e) && e == LedEffect::Aurora);
    CHECK(!ledEffectFromName("disco", e));
    CHECK(ledEffectName(LedEffect::Chase)[0] == 'c');
}

TEST_CASE(pending_fields_and_a_fresh_seq_per_take) {
    LightModel l;
    uint16_t fields = 0;
    CHECK_EQ(l.takePending(fields), 0u);  // nothing pending: no seq spent
    CHECK(l.setPower(true));               // already on: nothing pending
    CHECK_EQ(l.pendingFields(), 0u);
    CHECK(l.setPower(false));
    CHECK(l.adjustBrightness(3));
    CHECK(l.setEffect(LedEffect::Wave));
    CHECK_EQ(l.pendingFields(), static_cast<uint16_t>(LightModel::PendingPower | LightModel::PendingBrightness | LightModel::PendingEffect));
    CHECK(l.publishPending());
    CHECK_EQ(l.takePending(fields), 1u);
    CHECK_EQ(fields, static_cast<uint16_t>(LightModel::PendingPower | LightModel::PendingBrightness | LightModel::PendingEffect));
    CHECK_EQ(l.pendingFields(), 0u);
    const uint8_t params[kLedParamCount] = {1, 2, 3, 4, 5};
    CHECK(l.setParams(params));
    CHECK(l.setEffectColor(0x123456));
    CHECK_EQ(l.takePending(fields), 2u);
    CHECK_EQ(fields, static_cast<uint16_t>(LightModel::PendingParams | LightModel::PendingEffectColor));
    const LedStripState out = l.asStripState();
    CHECK_EQ(out.brightness, 53);
    CHECK(out.effect == LedEffect::Wave);
    CHECK_EQ(out.effectRgb, 0x123456u);
}

TEST_CASE(echoes_are_silent_stale_echoes_dropped_and_remote_changes_win) {
    LightModel l;
    uint16_t fields = 0;
    CHECK(l.setBrightness(70));
    CHECK_EQ(l.takePending(fields), 1u);
    CHECK(l.setBrightness(90));
    CHECK_EQ(l.takePending(fields), 2u);
    // The echo of set 1 arrives late: stale, ignored.
    CHECK(!l.adoptRemote(reported(true, 70, 0x00FFFF, LedEffect::Solid), LedSource::Self, 1));
    CHECK_EQ(l.state().brightness, 90);
    // The echo of set 2 with a clamp applied by the strip: applied, nothing pending.
    CHECK(l.adoptRemote(reported(true, 88, 0x00FFFF, LedEffect::Solid), LedSource::Self, 2));
    CHECK_EQ(l.state().brightness, 88);
    CHECK_EQ(l.pendingFields(), 0u);
    // A local change still pending survives the echo of the previous set.
    CHECK(l.adjustBrightness(5));
    CHECK(!l.adoptRemote(reported(true, 88, 0x00FFFF, LedEffect::Solid), LedSource::Self, 2));
    CHECK_EQ(l.state().brightness, 93);
    CHECK_EQ(l.pendingFields(), static_cast<uint16_t>(LightModel::PendingBrightness));
    // The web page wins: applied in full, the pending change is cancelled, still nothing to send.
    CHECK(l.adoptRemote(reported(false, 10, 0xFF00FF, LedEffect::Fire), LedSource::Web, 500));
    CHECK(!l.state().power);
    CHECK_EQ(l.state().brightness, 10);
    CHECK(l.state().effect == LedEffect::Fire);
    CHECK_EQ(l.pendingFields(), 0u);
    CHECK_EQ(l.sentSeq(), 2u);
}

TEST_CASE(a_200_detent_drag_yields_at_most_ten_publishes_per_second_with_a_trailing_flush) {
    LightModel l;
    int publishes = 0;
    for (int t = 0; t < 2000; t += 10) {
        l.adjustBrightness((t / 10) % 2 ? 1 : -1);
        if (l.shouldPublish(t)) ++publishes;
    }
    CHECK(publishes <= 21);
    CHECK(publishes >= 19);
    CHECK(l.publishPending());
    CHECK(l.flushPublish());
    CHECK(!l.publishPending());
}

TEST_CASE(light_model_allocates_nothing) {
    NO_HEAP_SCOPE();
    LightModel l;
    int notified = 0;
    CHECK(l.subscribe(&countNotify, &notified));
    uint16_t fields = 0;
    for (int i = 0; i < 5000; ++i) {
        l.setBrightness(i % 101);
        l.setHue(static_cast<float>(i % 100) / 100.0f);
        if (i % 7 == 0) l.stepEffect(1);
        if (i % 11 == 0) l.takePending(fields);
        l.commit();
    }
    CHECK(notified > 0);
}

TEST_CASE(an_older_echo_is_applied_once_nothing_is_pending_or_in_flight) {
    LightModel l;
    uint16_t fields = 0;
    CHECK(l.setBrightness(70));
    CHECK_EQ(l.takePending(fields), 1u);
    l.adoptRemote(reported(true, 70, 0x00FFFF, LedEffect::Solid), LedSource::Self, 1);  // the echo: nothing changes, but it acknowledges
    CHECK_EQ(l.ackedSeq(), 1u);
    // A replay of the retained state after a reconnect carries the truth: applied, not dropped.
    CHECK(l.adoptRemote(reported(true, 60, 0x00FFFF, LedEffect::Solid), LedSource::Self, 0));
    CHECK_EQ(l.state().brightness, 60);
    // A failed publish hands the fields back and re-arms the window.
    CHECK(l.setBrightness(80));
    CHECK_EQ(l.takePending(fields), 2u);
    CHECK_EQ(l.pendingFields(), 0u);
    l.restorePending(fields);
    CHECK_EQ(l.pendingFields(), static_cast<uint16_t>(LightModel::PendingBrightness));
    CHECK(l.publishPending());
    // While that set is pending, an older echo is stale again.
    CHECK(!l.adoptRemote(reported(true, 60, 0x00FFFF, LedEffect::Solid), LedSource::Self, 1));
    CHECK_EQ(l.state().brightness, 80);
}
