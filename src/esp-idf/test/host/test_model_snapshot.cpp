// test_model_snapshot (MDL-41): seqlock consistency. Two readers against one writer for 100k
// iterations, every snapshot from a single generation; the generation counter is always even
// between publishes; nothing allocates in publish/read.
#include <atomic>
#include <cstdint>
#include <thread>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/ModelSnapshot.h"

namespace {

// Three fields derived from one generation number: a torn read shows up as b != 2a or c != 3a.
struct Sample {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t padding[13];  // make the copy long enough that a torn read is actually possible
};

bool consistent(const Sample& s) {
    return s.b == s.a * 2u && s.c == s.a * 3u;
}

}  // namespace

TEST_CASE(single_thread_publish_and_read) {
    model::ModelSnapshot<Sample> snapshot;
    CHECK_EQ(snapshot.generation(), 0u);
    Sample s{};
    s.a = 5; s.b = 10; s.c = 15;
    snapshot.publish(s);
    CHECK_EQ(snapshot.generation(), 2u);
    const Sample got = snapshot.read();
    CHECK_EQ(got.a, 5u);
    CHECK(consistent(got));
    Sample again{};
    CHECK(snapshot.tryRead(again));
    CHECK_EQ(again.c, 15u);
}

TEST_CASE(two_readers_never_observe_a_torn_write) {
    model::ModelSnapshot<Sample> snapshot;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> reads{0};
    std::atomic<uint64_t> torn{0};

    auto reader = [&]() {
        while (!stop.load(std::memory_order_relaxed)) {
            const Sample s = snapshot.read();
            reads.fetch_add(1, std::memory_order_relaxed);
            if (!consistent(s)) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::thread r1(reader);
    std::thread r2(reader);
    for (uint32_t g = 1; g <= 100000u; ++g) {
        Sample s{};
        s.a = g; s.b = g * 2u; s.c = g * 3u;
        for (uint32_t& p : s.padding) {
            p = g;
        }
        snapshot.publish(s);
    }
    stop.store(true);
    r1.join();
    r2.join();

    CHECK_EQ(torn.load(), 0u);
    CHECK(reads.load() > 0u);
    CHECK_EQ(snapshot.generation(), 200000u);
    CHECK_EQ(snapshot.read().a, 100000u);
}

TEST_CASE(publish_and_read_allocate_nothing) {
    model::ModelSnapshot<Sample> snapshot;
    {
        NO_HEAP_SCOPE();
        for (uint32_t g = 0; g < 10000u; ++g) {
            Sample s{};
            s.a = g; s.b = g * 2u; s.c = g * 3u;
            snapshot.publish(s);
            CHECK(consistent(snapshot.read()));
        }
    }
}
