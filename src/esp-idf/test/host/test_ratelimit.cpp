// test_ratelimit (MDL-13): window boundaries (deny at n-1, allow at n), flush() always emitting
// the last value, a 100-change burst collapsing to one fire, the debouncer's single fire, the
// echo window, all on FakeClock and with zero allocations.
#include "FakeClock.h"
#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/RateLimit.h"

using namespace model;

TEST_CASE(first_publish_is_immediate_then_the_window_applies) {
    fakes::FakeClock clock;
    RateLimiter limiter(25);
    CHECK(!limiter.shouldPublish(clock.now));  // nothing pending
    limiter.markChanged();
    CHECK(limiter.shouldPublish(clock.now));   // first ever publish: no delay
    CHECK(!limiter.pending());

    limiter.markChanged();
    clock.advance(24);
    CHECK(!limiter.shouldPublish(clock.now));  // n - 1: denied
    clock.advance(1);
    CHECK(limiter.shouldPublish(clock.now));   // n: allowed
    CHECK_EQ(limiter.lastPublishMs(), static_cast<Millis>(25));
}

TEST_CASE(a_hundred_change_burst_collapses_to_one_fire_and_flush_emits_the_last) {
    fakes::FakeClock clock;
    RateLimiter limiter(25);
    limiter.markChanged();
    CHECK(limiter.shouldPublish(clock.now));

    int fires = 0;
    for (int i = 0; i < 100; ++i) {
        limiter.markChanged();
        clock.advance(0);
        if (limiter.shouldPublish(clock.now)) ++fires;
    }
    CHECK_EQ(fires, 0);          // all inside the window
    CHECK(limiter.pending());    // the last value is still waiting
    CHECK(limiter.flush());      // and flush emits it
    CHECK(!limiter.pending());
    CHECK(!limiter.flush());     // nothing left
}

TEST_CASE(debouncer_fires_once_after_the_quiet_period) {
    fakes::FakeClock clock;
    Debouncer debouncer(400);
    CHECK(!debouncer.settled(clock.now));
    for (int i = 0; i < 10; ++i) {
        debouncer.poke(clock.now);
        clock.advance(100);
        CHECK(!debouncer.settled(clock.now));  // never quiet for 400
    }
    clock.advance(299);
    CHECK(!debouncer.settled(clock.now));      // 399 since the last poke
    clock.advance(1);
    CHECK(debouncer.settled(clock.now));       // 400: fires
    CHECK(!debouncer.settled(clock.now));      // once
    CHECK(!debouncer.armed());
}

TEST_CASE(echo_suppressor_matches_value_inside_the_window_only) {
    fakes::FakeClock clock;
    EchoSuppressor echo(1500);
    CHECK(!echo.isEcho(42, clock.now));        // nothing published yet
    echo.published(42, clock.now);
    CHECK(echo.isEcho(42, clock.now));
    CHECK(!echo.isEcho(43, clock.now));        // a different value is a real change
    clock.advance(1500);
    CHECK(echo.isEcho(42, clock.now));         // boundary inclusive
    clock.advance(1);
    CHECK(!echo.isEcho(42, clock.now));        // expired
    echo.published(7, clock.now);
    echo.clear();
    CHECK(!echo.isEcho(7, clock.now));
}

TEST_CASE(policies_allocate_nothing) {
    NO_HEAP_SCOPE();
    fakes::FakeClock clock;
    RateLimiter limiter(10);
    Debouncer debouncer(400);
    EchoSuppressor echo(1500);
    int fires = 0;
    for (int i = 0; i < 10000; ++i) {
        limiter.markChanged();
        if (limiter.shouldPublish(clock.now)) ++fires;
        debouncer.poke(clock.now);
        echo.published(i, clock.now);
        (void)echo.isEcho(i, clock.now);
        clock.advance(1);
    }
    CHECK(fires > 900 && fires <= 1001);
}
