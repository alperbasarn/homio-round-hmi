// SCRN-08: the boot ramp on the host. 0..100 over 2500 ms posted as InitProgress records the model
// accepts without a backward step, one Info request 400 ms after the ramp completes, nothing
// before onEnter, one start stamp per run, the view destroyed on exit, and no heap on the way.
#include "BootScreenController.h"
#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/AppModel.h"

namespace {

class RecordingView final : public ui::ScreenView<model::NavigationState> {
public:
    int builds = 0;
    int destroys = 0;
    int renders = 0;
    int lastPct = -1;
    bool backwards = false;
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
    void doSetVisible(bool /*visible*/) override {}
    void doRender(const model::NavigationState& s) override {
        ++renders;
        if (s.initProgressPct < lastPct) {
            backwards = true;
        }
        lastPct = s.initProgressPct;
    }

private:
    int marker_ = 0;
    lv_obj_t* root_ = nullptr;
};

// One display frame as the device runs it: drain at the top (D1), then the controller tick.
struct Rig {
    model::AppModel app;
    RecordingView view;
    controllers::BootScreenController boot{&view, &app.mutations(), &app.navigation(), 466, 466};
    void frame(uint32_t now) {
        app.drainPendingMutations(now);
        boot.tick(now);
    }
};

}  // namespace

TEST_CASE(the_ramp_reaches_100_at_2500ms_and_requests_info_400ms_later) {
    Rig r;
    r.boot.onEnter(1000);
    CHECK_EQ(r.view.builds, 1);
    CHECK(r.view.built() && r.view.visible());
    int lastModel = -1;
    bool modelBackwards = false;
    uint32_t requestAt = 0;
    for (uint32_t t = 1000; t <= 4200; t += 16) {
        r.frame(t);
        const int pct = r.app.navigation().state().initProgressPct;
        if (pct < lastModel) {
            modelBackwards = true;
        }
        lastModel = pct;
        if (requestAt == 0 && r.boot.hasNavigationRequest()) {
            requestAt = t;
        }
    }
    CHECK(!modelBackwards);
    CHECK(!r.view.backwards);
    CHECK_EQ(lastModel, 100);
    CHECK_EQ(r.view.lastPct, 100);
    CHECK(r.boot.rampComplete());
    // 100 % lands on the first frame at or after 3500 (3512 on this grid); the request 400 ms later.
    CHECK_EQ(requestAt, 3912u);
    CHECK_EQ(r.app.rejectedRecords(), 0u);  // the model never had to refuse a step
    const controllers::NavigationRequest req = r.boot.takeNavigationRequest();
    CHECK(req.valid());
    CHECK(req.screen == model::AppScreen::Info);
    CHECK(!r.boot.takeNavigationRequest().valid());  // taken once; it does not come back
}

TEST_CASE(progress_is_exactly_100_at_2500ms_and_the_hold_is_400ms) {
    Rig r;
    r.boot.onEnter(0);
    r.frame(1250);
    CHECK_EQ(r.boot.progressPercent(), 50);
    r.frame(2500);
    CHECK_EQ(r.boot.progressPercent(), 100);
    CHECK(!r.boot.hasNavigationRequest());
    r.frame(2899);
    CHECK(!r.boot.hasNavigationRequest());
    r.frame(2900);
    CHECK(r.boot.hasNavigationRequest());
    CHECK_EQ(r.app.navigation().state().initProgressPct, 100);
}

TEST_CASE(nothing_before_enter_one_stamp_per_run_and_exit_destroys_the_view) {
    Rig r;
    r.boot.tick(5000);
    CHECK_EQ(r.view.builds, 0);
    CHECK_EQ(r.view.renders, 0);
    CHECK(!r.boot.hasNavigationRequest());
    r.boot.onEnter(10000);
    r.boot.onEnter(12000);  // a second enter while active keeps the first stamp (DISP-09)
    r.frame(11250);
    CHECK_EQ(r.boot.progressPercent(), 50);
    r.boot.onExit(11300);
    CHECK_EQ(r.view.destroys, 1);
    CHECK(!r.view.built());
    CHECK(!r.boot.active());
    r.boot.tick(20000);  // inert after exit
    CHECK_EQ(r.boot.progressPercent(), 50);
    r.boot.onEnter(30000);  // a fresh run restamps and rebuilds
    r.frame(30000);
    CHECK_EQ(r.boot.progressPercent(), 0);
    CHECK_EQ(r.view.builds, 2);
}

TEST_CASE(the_ramp_allocates_nothing) {
    Rig r;
    r.boot.onEnter(0);
    NO_HEAP_SCOPE();
    for (uint32_t t = 0; t <= 3000; t += 100) {
        r.frame(t);
    }
    CHECK(r.boot.hasNavigationRequest());
}
