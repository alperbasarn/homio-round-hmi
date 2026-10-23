// test_navigation_model (MDL-27): a request is consumed exactly once (a flag implementation
// fails request_consumed_once), the latest request wins, no transition to a dropped or
// impossible screen, every screen has a transition entry, one activity clock, boot progress
// never moves backwards, the wheel wraps, zero allocations.
#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/NavigationModel.h"

using namespace model;

namespace {

void countNotify(const NavigationState&, void* context) { ++*static_cast<int*>(context); }

}  // namespace

TEST_CASE(request_consumed_once) {
    NavigationModel n;
    CHECK(n.state().activeScreen == AppScreen::Boot);
    CHECK(!n.hasRequest());
    CHECK(n.requestScreen(AppScreen::Info, TransitionType::Animated));
    CHECK(n.hasRequest());
    const NavigationRequest first = n.takeRequest();
    CHECK(first.valid());
    CHECK(first.screen == AppScreen::Info);
    CHECK(first.transition == TransitionType::Animated);
    CHECK(!n.hasRequest());
    const NavigationRequest second = n.takeRequest();
    CHECK(!second.valid());                       // a flag-based implementation would still read Info here
    CHECK_EQ(n.state().requestsConsumed, 1u);
}

TEST_CASE(the_latest_request_replaces_an_unconsumed_one) {
    NavigationModel n;
    CHECK(n.setActiveScreen(AppScreen::Info));
    CHECK(n.requestScreen(AppScreen::DeviceInfo));
    CHECK(n.requestScreen(AppScreen::ModeWheel));
    CHECK(n.takeRequest().screen == AppScreen::ModeWheel);
    CHECK(!n.takeRequest().valid());
}

TEST_CASE(no_transition_to_a_dropped_or_impossible_screen) {
    NavigationModel n;
    CHECK(!n.requestScreen(AppScreen::Count));    // SLEEP is not a screen and has no enumerator
    CHECK(!n.requestScreen(AppScreen::Media));    // Boot only goes to Info
    CHECK(!n.hasRequest());
    CHECK(!NavigationModel::canTransition(AppScreen::Info, AppScreen::Info));
    CHECK(!NavigationModel::canTransition(AppScreen::DeviceInfo, AppScreen::Light));
    CHECK(NavigationModel::canTransition(AppScreen::Light, AppScreen::ModeWheel));
    CHECK(NavigationModel::canTransition(AppScreen::Climate, AppScreen::Info));
    CHECK(!n.setActiveScreen(AppScreen::Count));
}

TEST_CASE(every_screen_has_a_transition_entry) {
    int screensWithoutExit = 0;
    for (int from = 0; from < static_cast<int>(AppScreen::Count); ++from) {
        bool hasExit = false;
        for (int to = 0; to < static_cast<int>(AppScreen::Count); ++to) {
            if (NavigationModel::canTransition(static_cast<AppScreen>(from), static_cast<AppScreen>(to))) {
                hasExit = true;
            }
        }
        if (!hasExit) ++screensWithoutExit;
    }
    CHECK_EQ(screensWithoutExit, 0);
    CHECK(appScreenName(AppScreen::Climate)[0] == 'C');
    CHECK(knobModeName(KnobMode::Media)[0] == 'M');
}

TEST_CASE(one_activity_clock) {
    NavigationModel n;
    n.noteActivity(100);
    CHECK_EQ(n.idleMs(600), static_cast<Millis>(500));
    n.noteActivity(50);                           // an older stamp never rewinds the clock
    CHECK_EQ(n.state().lastActivityMs, static_cast<Millis>(100));
    CHECK_EQ(n.idleMs(50), static_cast<Millis>(0));
}

TEST_CASE(init_progress_is_the_only_home_for_boot_progress_and_never_moves_backwards) {
    NavigationModel n;
    CHECK(n.setInitProgress(40));
    CHECK_EQ(static_cast<int>(n.state().initProgressPct), 40);
    CHECK(!n.setInitProgress(30));
    CHECK_EQ(static_cast<int>(n.state().initProgressPct), 40);
    CHECK(n.setInitProgress(40));
    CHECK(n.setInitProgress(150));
    CHECK_EQ(static_cast<int>(n.state().initProgressPct), 100);
    n.resetInitProgress();
    CHECK(n.setInitProgress(10));
    CHECK(n.setActiveScreen(AppScreen::Info));    // leaving Boot completes the ramp
    CHECK_EQ(static_cast<int>(n.state().initProgressPct), 100);
}

TEST_CASE(the_wheel_wraps_in_both_directions) {
    NavigationModel n;
    CHECK(n.state().selectedMode == KnobMode::Media);
    CHECK(n.stepMode(1) == KnobMode::Light);
    CHECK(n.stepMode(-1) == KnobMode::Media);
    CHECK(n.stepMode(-1) == KnobMode::Climate);
    CHECK(n.stepMode(4) == KnobMode::Media);      // from Climate: a full lap plus one entry
    CHECK(n.selectMode(KnobMode::Light));
    CHECK(!n.selectMode(KnobMode::Count));
    CHECK(NavigationModel::screenFor(KnobMode::Light) == AppScreen::Light);
    CHECK(NavigationModel::screenFor(KnobMode::Climate) == AppScreen::Climate);
    CHECK(NavigationModel::screenFor(KnobMode::Media) == AppScreen::Media);
}

TEST_CASE(navigation_model_allocates_nothing) {
    NO_HEAP_SCOPE();
    NavigationModel n;
    int notified = 0;
    CHECK(n.subscribe(&countNotify, &notified));
    CHECK(n.setActiveScreen(AppScreen::Info));
    for (int i = 0; i < 5000; ++i) {
        n.noteActivity(i);
        n.requestScreen(i % 2 ? AppScreen::ModeWheel : AppScreen::DeviceInfo);
        (void)n.takeRequest();
        n.stepMode(1);
        n.commit();
    }
    CHECK(notified > 0);
}
