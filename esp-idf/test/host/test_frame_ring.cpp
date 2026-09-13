// test_frame_ring (LED-03): the inbound frame ring keeps order, drops on overflow and counts it,
// refuses frames that do not fit, tracks its high-water mark, and never allocates.
#include <cstring>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "adapters/FrameRing.h"

using namespace adapters;

TEST_CASE(frames_come_out_in_order_and_the_ring_drops_when_full) {
    FrameRing<4, 32, 64> ring;
    CHECK_EQ(ring.capacity(), static_cast<size_t>(3));
    CHECK(ring.push("t/1", 3, "one", 3));
    CHECK(ring.push("t/2", 3, "two", 3));
    CHECK(ring.push("t/3", 3, "three", 5));
    CHECK(!ring.push("t/4", 3, "four", 4));  // full: dropped and counted
    CHECK_EQ(ring.dropped(), 1u);
    CHECK_EQ(ring.size(), static_cast<size_t>(3));
    CHECK_EQ(ring.highWater(), static_cast<size_t>(3));
    FrameRing<4, 32, 64>::Frame f;
    CHECK(ring.pop(f));
    CHECK(std::strcmp(f.topic, "t/1") == 0 && std::strcmp(f.payload, "one") == 0);
    CHECK(ring.pop(f));
    CHECK(std::strcmp(f.payload, "two") == 0);
    CHECK(ring.push("t/5", 3, "five", 4));  // room again
    CHECK(ring.pop(f));
    CHECK(std::strcmp(f.payload, "three") == 0);
    CHECK(ring.pop(f));
    CHECK(std::strcmp(f.payload, "five") == 0);
    CHECK(!ring.pop(f));
    CHECK(ring.empty());
}

TEST_CASE(oversized_frames_are_refused_and_counted) {
    FrameRing<4, 8, 8> ring;
    CHECK(!ring.push("longer-than-eight", 17, "x", 1));
    CHECK(!ring.push("t", 1, "payload!", 8));  // 8 + terminator does not fit
    CHECK(ring.push("t", 1, "payload", 7));
    CHECK_EQ(ring.oversized(), 2u);
    CHECK_EQ(ring.dropped(), 0u);
    CHECK(!ring.push(nullptr, 0, "x", 1));
}

TEST_CASE(the_ring_allocates_nothing) {
    FrameRing<8, 64, 320> ring;
    NO_HEAP_SCOPE();
    FrameRing<8, 64, 320>::Frame f;
    for (int i = 0; i < 1000; ++i) {
        ring.push("homio/led/led-a1b2c3/state", 26, "{\"power\":true}", 14);
        if (i % 3 == 0) ring.pop(f);
    }
    CHECK(ring.dropped() > 0);
    while (ring.pop(f)) {
    }
    CHECK(ring.empty());
}
