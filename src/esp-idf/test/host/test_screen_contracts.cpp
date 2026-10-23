// SCRN-03 / SCRN-04 / BLD-43: the view lifecycle contract, the typed render, the controller base
// and the navigation outbox, on the host. There is no LVGL here: the lock assert compiles out and
// lv_obj_t is an opaque pointer, so a fake handle is any non-null address.
#include "HeapGuard.h"
#include "IScreenView.h"
#include "NullScreenController.h"
#include "ScreenController.h"
#include "TestRunner.h"
#include "Theme.h"

namespace {

struct FakeState {
    int value = 0;
};

class CountingView final : public ui::ScreenView<FakeState> {
public:
    int builds = 0;
    int destroys = 0;
    int renders = 0;
    int visibleCalls = 0;
    int lastRendered = -1;
    lv_obj_t* root() const override { return root_; }

protected:
    void doBuild(lv_obj_t* /*parent*/) override {
        ++builds;
        root_ = reinterpret_cast<lv_obj_t*>(&marker_);
    }
    void doDestroy() override {
        ++destroys;
        root_ = nullptr;
    }
    void doSetVisible(bool /*visible*/) override { ++visibleCalls; }
    void doRender(const FakeState& s) override {
        ++renders;
        lastRendered = s.value;
    }

private:
    int marker_ = 0;
    lv_obj_t* root_ = nullptr;
};

// A controller that navigates on a swipe: the shape every split screen will take.
class SwipingController final : public controllers::ScreenController {
public:
    model::AppScreen screen() const override { return model::AppScreen::Info; }
    bool onInput(const input::InputEvent& e, uint32_t /*nowMs*/) override {
        if (e.type == input::InputType::Gesture && e.gesture == input::Gesture::SwipeLeft) {
            requestScreen(model::AppScreen::ModeWheel, model::TransitionType::Animated);
            return true;
        }
        return false;
    }
};

// Exposes the protected outbox calls so the replace and cancel rules can be checked directly.
class OpenController final : public controllers::ScreenController {
public:
    using ScreenController::cancelNavigationRequest;
    using ScreenController::requestScreen;
    model::AppScreen screen() const override { return model::AppScreen::Info; }
};

input::InputEvent swipeLeft() {
    input::InputEvent e;
    e.type = input::InputType::Gesture;
    e.gesture = input::Gesture::SwipeLeft;
    return e;
}

}  // namespace

TEST_CASE(build_twice_then_destroy_twice_is_one_build_one_destroy_and_no_heap) {
    CountingView v;
    NO_HEAP_SCOPE();
    v.build(nullptr, 466, 466);
    v.build(nullptr, 466, 466);
    CHECK_EQ(v.builds, 1);
    CHECK(v.built() && v.visible() && v.root() != nullptr);
    CHECK_EQ(v.panelWidth(), 466);
    CHECK_EQ(v.panelHeight(), 466);
    v.destroy();
    v.destroy();
    CHECK_EQ(v.destroys, 1);
    CHECK(!v.built() && !v.visible() && v.root() == nullptr);
}

TEST_CASE(render_and_visibility_reach_the_view_only_while_built) {
    CountingView v;
    FakeState s;
    s.value = 7;
    v.render(s);
    v.setVisible(false);
    CHECK_EQ(v.renders, 0);
    CHECK_EQ(v.visibleCalls, 0);
    v.build(nullptr, 240, 240);
    v.render(s);
    CHECK_EQ(v.renders, 1);
    CHECK_EQ(v.lastRendered, 7);
    v.setVisible(true);  // already visible after build: nothing to do
    CHECK_EQ(v.visibleCalls, 0);
    v.setVisible(false);
    CHECK_EQ(v.visibleCalls, 1);
    CHECK(!v.visible());
    v.destroy();
    v.render(s);
    CHECK_EQ(v.renders, 1);
}

TEST_CASE(scale_px_uses_the_injected_panel_width_and_never_returns_zero) {
    CountingView v;
    v.build(nullptr, 466, 466);
    CHECK_EQ(v.scalePx(240), 466);
    CHECK_EQ(v.scalePx(120), 233);
    CHECK_EQ(v.scalePx(0), 1);
    CHECK_EQ(ui::theme::scalePx(10, 240), 10);
    CHECK_EQ(ui::theme::scalePx(1, 0), 1);  // unknown panel: still drawable
    CHECK_EQ(ui::theme::scalePx(240, 240), 240);
}

TEST_CASE(a_navigation_request_is_taken_exactly_once) {
    SwipingController c;
    NO_HEAP_SCOPE();
    CHECK(!c.hasNavigationRequest());
    CHECK(!c.takeNavigationRequest().valid());
    CHECK(c.onInput(swipeLeft(), 100));
    CHECK(c.hasNavigationRequest());
    const controllers::NavigationRequest r = c.takeNavigationRequest();
    CHECK(r.valid());
    CHECK(r.screen == model::AppScreen::ModeWheel);
    CHECK(r.transition == model::TransitionType::Animated);
    CHECK(!c.hasNavigationRequest());
    CHECK(!c.takeNavigationRequest().valid());
}

TEST_CASE(the_latest_request_replaces_an_unconsumed_one_and_can_be_cancelled) {
    OpenController c;
    c.requestScreen(model::AppScreen::Media);
    c.requestScreen(model::AppScreen::Light, model::TransitionType::Animated);
    const controllers::NavigationRequest r = c.takeNavigationRequest();
    CHECK(r.screen == model::AppScreen::Light);
    CHECK(r.transition == model::TransitionType::Animated);
    c.requestScreen(model::AppScreen::Media);
    c.cancelNavigationRequest();
    CHECK(!c.hasNavigationRequest());
    CHECK(!c.takeNavigationRequest().valid());
}

TEST_CASE(a_null_controller_stands_in_consumes_nothing_and_requests_nothing) {
    controllers::NullScreenController n(model::AppScreen::Climate);
    input::InputEvent press;
    press.type = input::InputType::TouchPress;
    CHECK(n.screen() == model::AppScreen::Climate);
    CHECK(!n.onInput(press, 0));
    CHECK(!n.onInput(swipeLeft(), 16));
    n.onEnter(0);
    n.tick(16);
    n.onExit(32);
    CHECK(!n.hasNavigationRequest());
    CHECK(!n.takeNavigationRequest().valid());
}
