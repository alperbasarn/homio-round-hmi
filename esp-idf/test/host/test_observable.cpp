// test_observable (MDL-04, MDL-06): capacity, over-capacity refusal, notify-on-change only,
// context delivery, and zero allocations over 10k sets.
#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/ModelTypes.h"
#include "model/Observable.h"

namespace {

struct Counter {
    int calls = 0;
    int lastValue = 0;
};

void onInt(const int& value, void* context) {
    auto* counter = static_cast<Counter*>(context);
    ++counter->calls;
    counter->lastValue = value;
}

struct Pair {
    int a = 0;
    int b = 0;
    bool operator==(const Pair& other) const { return a == other.a && b == other.b; }
};

void onPair(const Pair&, void* context) { ++static_cast<Counter*>(context)->calls; }

}  // namespace

TEST_CASE(subscribe_refuses_when_full) {
    model::Observable<int, 2> observable;
    Counter c1, c2, c3;
    CHECK(observable.subscribe(&onInt, &c1));
    CHECK(observable.subscribe(&onInt, &c2));
    CHECK(!observable.subscribe(&onInt, &c3));
    CHECK(!observable.subscribe(nullptr, &c1));
    CHECK_EQ(observable.subscriberCount(), static_cast<size_t>(2));
}

TEST_CASE(set_notifies_only_on_change) {
    model::Observable<int, 4> observable(7);
    Counter counter;
    REQUIRE(observable.subscribe(&onInt, &counter));

    CHECK(!observable.set(7));  // equal value: nobody hears about it
    CHECK_EQ(counter.calls, 0);

    CHECK(observable.set(8));
    CHECK_EQ(counter.calls, 1);
    CHECK_EQ(counter.lastValue, 8);
    CHECK_EQ(observable.get(), 8);

    CHECK(!observable.set(8));
    CHECK_EQ(counter.calls, 1);
}

TEST_CASE(every_subscriber_receives_its_own_context) {
    model::Observable<int, 3> observable;
    Counter c1, c2;
    REQUIRE(observable.subscribe(&onInt, &c1));
    REQUIRE(observable.subscribe(&onInt, &c2));
    observable.set(42);
    CHECK_EQ(c1.calls, 1);
    CHECK_EQ(c2.calls, 1);
    CHECK_EQ(c1.lastValue, 42);
    CHECK_EQ(c2.lastValue, 42);
}

TEST_CASE(struct_values_use_their_equality) {
    model::Observable<Pair, 2> observable;
    Counter counter;
    REQUIRE(observable.subscribe(&onPair, &counter));
    CHECK(observable.set(Pair{1, 2}));
    CHECK(!observable.set(Pair{1, 2}));
    CHECK(observable.set(Pair{1, 3}));
    CHECK_EQ(counter.calls, 2);
}

TEST_CASE(fixed_string_values_compare_by_content) {
    model::Observable<model::FixedString<16>, 1> observable;
    Counter counter;
    REQUIRE(observable.subscribe([](const model::FixedString<16>&, void* ctx) { ++static_cast<Counter*>(ctx)->calls; }, &counter));
    CHECK(observable.set(model::FixedString<16>("Midnight City")));
    CHECK(!observable.set(model::FixedString<16>("Midnight City")));
    CHECK(observable.set(model::FixedString<16>("Midnight Cit")));
    CHECK_EQ(counter.calls, 2);
    model::FixedString<16> longer;
    CHECK(!longer.assign("this string is longer than sixteen"));  // truncated
    CHECK_EQ(longer.size(), static_cast<size_t>(16));
}

TEST_CASE(ten_thousand_sets_allocate_nothing) {
    model::Observable<int, 4> observable(-1);  // distinct from every value the loop sets
    Counter counter;
    REQUIRE(observable.subscribe(&onInt, &counter));
    {
        NO_HEAP_SCOPE();
        for (int i = 0; i < 10000; ++i) {
            observable.set(i);
        }
    }
    CHECK_EQ(counter.calls, 10000);
    CHECK_EQ(counter.lastValue, 9999);
}
